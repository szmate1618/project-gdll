#!/usr/bin/env python3
"""Render reproducible steering trajectories and physical trunk footprints."""
import argparse
import colorsys
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

MODES=['collision_only','gradient','gradient_side','perpendicular_push','detour']
LABELS=['Collision response','Gradient only','Gradient + side','Perpendicular push','Detour avoidance']
COLORS=['#6c6b73','#397cbd','#2d9870','#d77e23','#9a56ad']


def font(size,bold=False):
    return ImageFont.truetype('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf',size)


def load(comparison,name,mode):
    entry=comparison['scenarios'][name]['modes'][mode]
    document=json.loads((Path(entry['representative_output'])/'summary.json').read_text())
    scene=next(s for s in document['scenarios'] if s['name']==name)
    return entry,scene,scene['modes'][0]


def panel(canvas,box,scene,result,color,body_radius=.3,many=False):
    x0,y0,x1,y1=box
    bounds=scene['geometry']['bounds'];loX,loZ,hiX,hiZ=bounds
    scale=min((x1-x0-12)/(hiX-loX),(y1-y0-12)/(hiZ-loZ))
    left=(x0+x1-(hiX-loX)*scale)/2;top=(y0+y1-(hiZ-loZ)*scale)/2
    def p(point):return (left+(point[0]-loX)*scale,top+(point[1]-loZ)*scale)
    draw=ImageDraw.Draw(canvas)
    draw.rectangle((left,top,left+(hiX-loX)*scale,top+(hiZ-loZ)*scale),fill='#ffffff',outline='#c9d0d3',width=1)
    for t in scene['geometry']['trunks']:
        x,y=p([t['base'][0],t['base'][2]])
        inflated=(t['radius']+body_radius)*scale
        r=max(.6,t['radius']*scale)
        draw.ellipse((x-inflated,y-inflated,x+inflated,y+inflated),fill='#f1e7dc',outline='#deccb8')
        draw.ellipse((x-r,y-r,x+r,y+r),fill='#77634d')
    layer=Image.new('RGBA',canvas.size);routes=ImageDraw.Draw(layer)
    for agent,request in zip(result['agents'],scene['requests']):
        points=[p(point) for point in agent['trace']]
        if many:
            hue=(agent['id']*.618033988749895)%1
            c=tuple(round(v*255) for v in colorsys.hsv_to_rgb(hue,.72,.66))+(105,)
        else:
            c=color
        if len(points)>1:routes.line(points,fill=c,width=1 if many else 3)
        if not many:
            x,y=p(request['start']);routes.ellipse((x-4,y-4,x+4,y+4),fill='#159c75')
            x,y=p(request['goal']);routes.polygon([(x,y-6),(x+5,y),(x,y+6),(x-5,y)],fill='#d02f73')
        if points and not agent['arrived']:
            x,y=points[-1];r=2 if many else 5
            routes.line((x-r,y-r,x+r,y+r),fill='#d93435',width=1 if many else 2)
            routes.line((x-r,y+r,x+r,y-r),fill='#d93435',width=1 if many else 2)
    canvas.alpha_composite(layer)


def render(run):
    comparison=json.loads((run/'comparison.json').read_text())
    names=[name for name in comparison['scenarios'] if name!='woodland']
    width,cell_w,cell_h,header,left=2040,370,240,120,180
    image=Image.new('RGBA',(width,header+len(names)*cell_h+80),'#f6f7f8')
    d=ImageDraw.Draw(image)
    d.text((20,15),'Five local steering rules · controlled obstacle cases',font=font(29,True),fill='#203440')
    d.text((20,57),'Same path, 2 m/s limit, 0.05 s timestep and hard collision guards · X east / Z south',font=font(18),fill='#536673')
    for column,label in enumerate(LABELS):
        d.text((left+column*cell_w+10,93),label,font=font(20,True),fill=COLORS[column])
    for row,name in enumerate(names):
        top=header+row*cell_h
        label=name.replace('_',' ')
        words=label.split();lines=[];line=''
        for word in words:
            if len(line)+len(word)>16:lines.append(line);line=''
            line=(line+' '+word).strip()
        lines.append(line)
        d.multiline_text((12,top+70),'\n'.join(lines),font=font(17,True),fill='#334955',spacing=5)
        for column,mode in enumerate(MODES):
            entry,scene,result=load(comparison,name,mode)
            x=left+column*cell_w
            panel(image,(x+4,top+2,x+cell_w-5,top+190),scene,result,COLORS[column])
            d=ImageDraw.Draw(image)
            arrived=entry['arrived']
            seconds=result['agents'][0]['arrival_seconds']
            status=f'Arrived in {seconds:.2f} s' if arrived else 'Not arrived within 60 s'
            d.text((x+12,top+197),status,font=font(16,True),fill='#236e52' if arrived else '#ab3e3e')
            d.text((x+12,top+220),f"Travel {result['agents'][0]['travel_m']:.2f} m",font=font(14),fill='#536673')
    d=ImageDraw.Draw(image)
    d.text((20,image.height-59),'Brown: trunk · Pale ring: trunk expanded by NPC radius · Green: start · Pink: goal · Red ×: nonarrival',font=font(17),fill='#334955')
    d.text((20,image.height-32),'Trajectories are decimated for display; clearance validation uses every accepted simulation step.',font=font(16),fill='#536673')
    image.convert('RGB').save(run/'controlled_paths.png')
    image=Image.new('RGBA',(1860,1460),'#f6f7f8');d=ImageDraw.Draw(image)
    d.text((20,16),'1,000 independent NPCs · shared synthetic woodland',font=font(29,True),fill='#203440')
    d.text((20,59),'500 trunks · identical seeded starts/goals · 60 s simulation · NPC-to-NPC collision disabled',font=font(18),fill='#536673')
    for index,mode in enumerate(MODES):
        x=(index%3)*620;y=105+(index//3)*660
        entry,scene,result=load(comparison,'woodland',mode)
        d=ImageDraw.Draw(image);d.text((x+15,y),LABELS[index],font=font(23,True),fill=COLORS[index])
        d.text((x+15,y+34),f"{entry['arrived']:,}/1,000 arrived · {entry['stalled']} stalled",font=font(18),fill='#334955')
        panel(image,(x+15,y+65,x+605,y+635),scene,result,COLORS[index],many=True)
        d=ImageDraw.Draw(image)
        timing=entry['fixed_1000_active_step_ms']['mean']
        d.text((x+15,y+640),f'{timing:.3f} ms/step with all 1,000 active',font=font(17),fill='#334955')
    d=ImageDraw.Draw(image)
    x,y=1260,830
    d.text((x,y),'Comparison conditions',font=font(23,True),fill='#203440')
    lines=['Tree-free navmesh routes, g + 8h.','Same follower and collision handling.','No controller-specific replanning.','Red × marks an unfinished route.','','Timing uses the first 5 simulation seconds,','before any NPC can reach its destination.','','Controlled fixtures are shown separately.','These are synthetic isolation tests,','not a full-town or rendering benchmark.']
    d.multiline_text((x,y+45),'\n'.join(lines),font=font(18),fill='#536673',spacing=10)
    image.convert('RGB').save(run/'woodland_paths.png')
    print(run/'controlled_paths.png');print(run/'woodland_paths.png')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path)
    render(p.parse_args().run)
