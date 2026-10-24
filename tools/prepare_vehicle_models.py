#!/usr/bin/env python3
"""Prepare lightweight CC BY vehicles and render matched comparisons."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.vehicle_assets import embed_gltf, selected_document, simplify_vehicle, triangle_count, validated_glb, merge_identical_solid_materials
from src.vehicle_asset_render import geometry, reference_for, render
from tools.download_vehicle_models import atomic, json_bytes


def font(size):
    return ImageFont.truetype('DejaVuSans.ttf', size)


def comparison(original, reduced, record, output):
    before, after = geometry(original), geometry(reduced)
    reference = reference_for(before)
    views = [('View A', (1.3,.65,1.6)), ('Opposite view', (-1.3,.65,-1.6)), ('Side / topology', (1.,.25,0.))]
    canvas = Image.new('RGB', (1200, 1160), '#131923')
    draw = ImageDraw.Draw(canvas)
    draw.text((28,18),record['name'],font=font(26),fill='#eff5ff')
    draw.text((28,58),f"Original: {record['before']:,} triangles",font=font(20),fill='#b7c9df')
    action = 'geometry retained' if not record['simplified'] else f"{100*(1-record['after']/record['before']):.1f}% fewer triangles"
    draw.text((628,58),f"Output: {record['after']:,} triangles | {action}",font=font(20),fill='#76deb5')
    for i,(label,direction) in enumerate(views):
        y = 98+i*340
        canvas.paste(render(before,reference,direction,size=(600,320),wire=i==2),(0,y))
        canvas.paste(render(after,reference,direction,size=(600,320),wire=i==2),(600,y))
        draw.text((18,y+6),label,font=font(16),fill='#b7c9df')
        draw.text((618,y+6),label,font=font(16),fill='#b7c9df')
    draw.text((24,1126),'CC BY 4.0 | '+record['author']+' | CPU preview; matching scale, camera and lighting',font=font(15),fill='#97abc4')
    atomic(output, image_bytes(canvas))
    return canvas.crop((0,0,1200,418))


def image_bytes(image):
    import io
    stream=io.BytesIO()
    image.save(stream,format='PNG',optimize=True)
    return stream.getvalue()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,default=ROOT/'assets/vehicles')
    parser.add_argument('--helper',type=Path,default=ROOT/'build/vehicle_mesh_simplify')
    args=parser.parse_args()
    root=args.assets
    manifest=json.loads((root/'sources.json').read_text())
    records=[]
    cards=[]
    import inspect
    import src.vehicle_asset_render as renderer
    render_signature=hashlib.sha256(Path(renderer.__file__).read_bytes()+inspect.getsource(comparison).encode()).hexdigest()
    for row in manifest['models']:
        if not row.get('license_eligible') or row.get('download_status')!=200:
            continue
        doc,binary=embed_gltf(root/row['scene_path'])
        for variant in row.get('variants',[dict(name='',root_node=None)]):
            suffix=variant['name']
            slug=row['slug']+('-'+suffix if suffix else '')
            name=row['requested_name']+(' / '+suffix if suffix else '')
            original=selected_document(doc,variant['root_node'])
            budget=row.get('triangle_budget',999)
            input_doc,input_binary=original,binary
            if row.get('merge_identical_solid_materials'):
                input_doc,input_binary=merge_identical_solid_materials(original,binary)
            reduced,bin2,stats=simplify_vehicle(input_doc,input_binary,args.helper.resolve(),maximum=budget)
            original_bytes=validated_glb(original,binary)
            reduced_bytes=validated_glb(reduced,bin2)
            from src.glb import read_glb
            if triangle_count(read_glb(reduced_bytes)[0])!=stats['after']:
                raise ValueError('Serialized triangle count mismatch: '+slug)
            output_suffix='under-1000' if budget==999 else 'optimized'
            record=dict(slug=slug,name=name,source_slug=row['slug'],author=row['author'],source_url=row['url'],triangle_budget=budget,
                        original_glb='models/'+slug+'-original.glb',output_glb='models/'+slug+'-'+output_suffix+'.glb',
                        comparison='comparisons/'+slug+'.png',
                        original_sha256=hashlib.sha256(original_bytes).hexdigest(),
                        output_sha256=hashlib.sha256(reduced_bytes).hexdigest(),**stats)
            cache_path=root/'models'/(slug+'-preview.json')
            cache=json.loads(cache_path.read_text()) if cache_path.exists() else {}
            image_path=root/record['comparison']
            expected={key:record[key] for key in ('original_sha256','output_sha256')}
            expected['render_signature']=render_signature
            valid=all(cache.get(k)==v for k,v in expected.items()) and image_path.exists()
            valid=valid and hashlib.sha256(image_path.read_bytes()).hexdigest()==cache.get('comparison_sha256')
            atomic(root/record['original_glb'],original_bytes)
            atomic(root/record['output_glb'],reduced_bytes)
            if valid:
                with Image.open(image_path) as existing:
                    cards.append(existing.crop((0,0,1200,418)))
            else:
                cards.append(comparison(original_bytes,reduced_bytes,record,image_path))
                expected['comparison_sha256']=hashlib.sha256(image_path.read_bytes()).hexdigest()
                atomic(cache_path,json_bytes(expected))
            records.append(record)
            print(f"{slug}: {stats['before']:,} -> {stats['after']:,} triangles; comparison rendered",flush=True)
    columns=2
    rows=(len(cards)+columns-1)//columns
    sheet=Image.new('RGB',(1600,round(rows*418*2/3)+104),'#101722')
    draw=ImageDraw.Draw(sheet)
    draw.text((25,18),'VEHICLE MESH COMPARISONS',font=font(30),fill='white')
    draw.text((25,60),'Original on left | selected output on right | 999 triangles by default; up to 1,500 where useful',font=font(18),fill='#a7bfd9')
    for i,card in enumerate(cards):
        sheet.paste(card.resize((800,round(418*2/3)),Image.Resampling.LANCZOS),((i%columns)*800,104+(i//columns)*round(418*2/3)))
    atomic(root/'comparisons/overview.png',image_bytes(sheet))
    atomic(root/'preparation.json',json_bytes({'default_maximum_triangles':999,'maximum_triangles':1500,'meshoptimizer':'1.3',
        'preview':'Deterministic textured CPU rasterization; no normal maps, environment reflections or PBR specular highlights.',
        'variants':records}))


if __name__=='__main__':
    main()
