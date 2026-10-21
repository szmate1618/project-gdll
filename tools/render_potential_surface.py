#!/usr/bin/env python3
"""Render direct potential-surface NPC trajectories on the original map raster."""
import argparse
import json
import math
from pathlib import Path
import statistics

from PIL import Image, ImageDraw, ImageFont

COLORS = {'arrived': '#00a879', 'stalled': '#d74748', 'timeout': '#d49421',
          'invalid_endpoint': '#8753a7'}


def font(size, bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf', size)
    except OSError:
        return ImageFont.load_default()


def source_path(document, summary):
    source = document['source']
    if isinstance(source, dict):
        source = source.get('file', source.get('path'))
    path = Path(source)
    return path if path.is_absolute() else summary.parent/path


def draw_geometry(image, obstacles, project, scale, details=False, center=None, half_span=None):
    draw = ImageDraw.Draw(image)
    for kind, x, z, ux, uz, hx, hz in obstacles:
        if details and center is not None and (abs(x-center[0]) > half_span+hx+hz or
                                               abs(z-center[1]) > half_span+hx+hz):
            continue
        length = math.hypot(ux, uz)
        ux, uz = ux/length, uz/length
        if kind == 1:
            px, py = project((x, z))
            r = max(.45 if not details else 1, max(hx, hz)*scale)
            draw.ellipse((px-r, py-r, px+r, py+r), fill=(40, 112, 47, 115))
            if details:
                padded = (max(hx, hz)+.302)*scale
                draw.ellipse((px-padded, py-padded, px+padded, py+padded), outline=(45, 100, 40, 140))
        else:
            points = [project((x+a*ux*hx-b*uz*hz, z+a*uz*hx+b*ux*hz))
                      for a,b in ((-1,-1),(1,-1),(1,1),(-1,1))]
            draw.polygon(points, fill=(163, 64, 48, 95) if kind == 0 else (167, 102, 31, 150))
            if details:
                draw.line(points+[points[0]], fill=(120, 65, 49, 190), width=1)


def symbol(draw, point, kind, color, radius=3):
    x, y = point
    if kind == 'start':
        draw.ellipse((x-radius,y-radius,x+radius,y+radius), fill=color, outline='white')
    elif kind == 'goal':
        draw.polygon(((x,y-radius-1),(x+radius+1,y),(x,y+radius+1),(x-radius-1,y)), fill=color)
    elif kind == 'end':
        draw.line((x-radius,y-radius,x+radius,y+radius), fill=color, width=2)
        draw.line((x-radius,y+radius,x+radius,y-radius), fill=color, width=2)


def detail(scene, background, raster, agent, title, size=560):
    center = agent['final']
    half = 35.0
    scale = size/(2*half)
    low = [center[0]-half, center[1]-half]
    def project(point):
        return ((point[0]-low[0])*scale, (point[1]-low[1])*scale)
    def original(point):
        return ((point[0]-raster['minimum'][0])*raster['scale']+raster['margin'],
                (point[1]-raster['minimum'][1])*raster['scale']+raster['margin'])
    a, b = original(low), original((low[0]+2*half, low[1]+2*half))
    image = background.crop((*a,*b)).resize((size,size), Image.Resampling.BICUBIC).convert('RGBA')
    layer = Image.new('RGBA', image.size)
    draw_geometry(layer, scene['obstacles'], project, scale, True, center, half)
    image = Image.alpha_composite(image, layer)
    layer = Image.new('RGBA', image.size); draw = ImageDraw.Draw(layer)
    trace = [project(p) for p in agent['trace']]
    if len(trace)>1:
        draw.line(trace, fill=COLORS.get(agent['status'],'#8753a7'), width=3)
    symbol(draw, project(agent['start']), 'start', '#1069bf', 5)
    symbol(draw, project(agent['goal']), 'goal', '#b62d9c', 6)
    symbol(draw, project(agent['final']), 'end', COLORS.get(agent['status'],'#8753a7'), 6)
    goal = project(agent['goal'])
    if not (0 <= goal[0] <= size and 0 <= goal[1] <= size):
        dx, dz = agent['goal'][0]-center[0], agent['goal'][1]-center[1]
        length = math.hypot(dx,dz)
        if length:
            direction = (dx/length, dz/length)
            start = (size/2+direction[0]*20, size/2+direction[1]*20)
            end = (size/2+direction[0]*size*.39, size/2+direction[1]*size*.39)
            draw.line((*start,*end), fill='#b62d9c', width=2)
            normal = (-direction[1],direction[0])
            draw.polygon((end,(end[0]-10*direction[0]+5*normal[0],end[1]-10*direction[1]+5*normal[1]),
                          (end[0]-10*direction[0]-5*normal[0],end[1]-10*direction[1]-5*normal[1])), fill='#b62d9c')
    image = Image.alpha_composite(image, layer)
    caption = Image.new('RGBA',(size,size+85),'#f6f7f8');caption.paste(image,(0,58))
    draw = ImageDraw.Draw(caption)
    remaining = math.dist(agent['final'],agent['goal'])
    draw.text((3,1),f"{title} · NPC {agent['id']}",font=font(20,True),fill='#233e4b')
    draw.text((3,29),f"{agent['status']} · {agent['travel_m']:.1f} m travelled · {remaining:.1f} m left",font=font(16),fill='#536773')
    draw.text((3,size+62),'70 × 70 m · purple arrow points toward off-screen goal',font=font(14),fill='#536773')
    return caption


def render(summary, output):
    document = json.loads(summary.read_text())
    source = source_path(document,summary)
    scene = json.loads(source.read_text())
    raster = document.get('raster',scene['raster'])
    raster_path = Path(raster['file'])
    if not raster_path.is_absolute():
        local = summary.parent/raster_path
        raster_path = local if local.exists() else source.parent/raster_path
    background = Image.open(raster_path).convert('RGBA')
    def project(point):
        return ((point[0]-raster['minimum'][0])*raster['scale']+raster['margin'],
                (point[1]-raster['minimum'][1])*raster['scale']+raster['margin'])
    layer = Image.new('RGBA',background.size)
    draw_geometry(layer,scene['obstacles'],project,raster['scale'])
    overview = Image.alpha_composite(background,layer)
    layer = Image.new('RGBA',background.size);draw=ImageDraw.Draw(layer)
    agents = document['agents']
    for agent in agents:
        trace = [project(point) for point in agent['trace']]
        color = COLORS.get(agent['status'],'#8753a7')
        if len(trace)>1:
            draw.line(trace,fill=color+'b0',width=1)
    for agent in agents:
        symbol(draw,project(agent['start']),'start','#1069bf',1.5)
        symbol(draw,project(agent['goal']),'goal','#b62d9c',2)
        symbol(draw,project(agent['final']),'end',COLORS.get(agent['status'],'#8753a7'),2)
    overview = Image.alpha_composite(overview,layer)
    header,footer,panel = 278,105,590
    canvas = Image.new('RGBA',(background.width+panel+60,background.height+header+footer),'#f6f7f8')
    canvas.paste(overview,(20,header));draw=ImageDraw.Draw(canvas)
    summary_metrics = document['summary']
    statuses = summary_metrics['statuses']
    total_values = [s['total_ms'] for s in document['samples']]
    cpu_values = [s['cpu_ms'] for s in document['samples']]
    configuration = document.get('configuration', {})
    steering = configuration.get('steering_mode', 'filtered')
    steering_label = {
        'filtered': 'analytical uphill filter',
        'normalized': 'uphill filter + walking-speed normalization',
        'tangent': 'normalized filter + persistent tangent steering',
    }.get(steering, steering)
    building_field = ('exact oriented rectangles' if configuration.get('exact_buildings', False)
                      else 'circumscribed circles')
    tangent_enabled = steering == 'tangent'
    draw.text((25,17),f"Potential-surface motion · {len(agents):,} independent NPCs",font=font(31,True),fill='#173440')
    draw.text((25,64),f'Direct goals · {steering_label} · no navigation mesh, paths or search',font=font(20),fill='#3d5965')
    draw.text((25,102),' · '.join(f'{statuses.get(k,0):,} {k.replace("_"," ")}' for k in COLORS),font=font(22,True),fill='#233e4b')
    draw.text((25,140),f"Median update-loop elapsed {statistics.median(total_values)/1000:.3f} s · CPU {statistics.median(cpu_values)/1000:.3f} s · {len(total_values)} measured repeats",font=font(19),fill='#536773')
    counts = scene['obstacle_counts']
    draw.text((25,175),f"Actual map · {counts[0]:,} building boxes · {counts[1]:,} trunks · {counts[2]:,} fence boxes · X east / Z south",font=font(19),fill='#536773')
    draw.text((25,208),f'Building potential fields: {building_field} · persistent tangent recovery: {"enabled" if tangent_enabled else "disabled"}',font=font(19),fill='#536773')
    draw.text((25,242),'Color: green reached · red stalled · amber timeout · blue start dot · purple destination diamond · × final position',font=font(18),fill='#536773')
    draw.text((background.width-65,header+20),'N ↑',font=font(25,True),fill='#173440')
    reached = [a for a in agents if a['status']=='arrived']
    stalled = [a for a in agents if a['status']=='stalled']
    unfinished = [a for a in agents if a['status']!='arrived']
    first = max(reached,key=lambda a:a['initial_distance_m'],default=None)
    if first is None:
        first = max(agents,key=lambda a:a['initial_distance_m']-math.dist(a['final'],a['goal']))
    second = max(stalled or unfinished or agents,key=lambda a:a['travel_m'])
    remaining = [a for a in (stalled or unfinished or agents) if a['id'] not in {first['id'],second['id']}]
    third = max(remaining or agents,key=lambda a:a['heading_radians']/max(.01,a['travel_m']))
    selected = [(first,'Reached destination' if first['status']=='arrived' else 'Largest goal progress'),
                (second,'Stall detail' if second['status']=='stalled' else 'Final-position detail'),
                (third,'Turning / blockage detail')]
    for index,(agent,title) in enumerate(selected):
        image = detail(scene,background,raster,agent,title)
        canvas.paste(image,(background.width+45,header+index*655))
    draw=ImageDraw.Draw(canvas)
    y=header+background.height+18
    draw.text((25,y),'Hard physical clipping uses original building/fence rectangles and circular trunks; field approximations do not change collision geometry.',font=font(18),fill='#536773')
    audit_label = ('Full movement stream records every accepted step for independent safety auditing.'
                   if document.get('moves_file') else
                   'Full movement logging disabled; native geometry checks still cover every timestep.')
    draw.text((25,y+30),'Display trajectories are decimated. ' + audit_label,font=font(18),fill='#536773')
    recovery_label = ('Persistent tangent recovery enabled.' if tangent_enabled else 'Tangent recovery disabled.')
    draw.text((25,y+60),recovery_label + ' No NPC-to-NPC avoidance, global connectivity test or route search.',font=font(18),fill='#536773')
    output.parent.mkdir(parents=True,exist_ok=True)
    canvas.convert('RGB').save(output)
    print(output)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    arguments=parser.parse_args()
    render(arguments.summary,arguments.output)
