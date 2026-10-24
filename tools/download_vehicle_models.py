#!/usr/bin/env python3
"""Download official Sketchfab glTF archives for the reviewed vehicle candidates."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.request
import zipfile


def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        f.write(data)
        temporary = Path(f.name)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def json_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=False)+'\n').encode()


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('Refusing to redirect an authenticated API request')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--token-file', type=Path)
    args=parser.parse_args()
    token=args.token_file.read_text().strip() if args.token_file else os.environ.get('SKETCHFAB_API_TOKEN','').strip()
    if not token or '\n' in token:
        parser.error('Provide SKETCHFAB_API_TOKEN or a --token-file containing one nonempty line')
    manifest_path=args.assets/'sources.json'
    manifest=json.loads(manifest_path.read_text())

    def download(row):
        row=dict(row)
        slug=re.sub('[^a-z0-9]+','-',row['requested_name'].lower()).strip('-')
        row['slug']=slug
        api=f"https://api.sketchfab.com/v3/models/{row['uid']}"
        with urllib.request.urlopen(api,timeout=45) as response:
            metadata=json.load(response)
        license=metadata.get('license') or {}
        if license.get('slug') not in ('by','cc0') or not metadata.get('isDownloadable'):
            row.update(license_eligible=False,download_status='excluded',license=license)
            print(slug+': excluded by current license/download policy',flush=True)
            return row
        row.update(license=license,listed_triangles=metadata['faceCount'],author=metadata['user']['displayName'],
                   author_url=metadata['user']['profileUrl'],license_eligible=True)
        atomic(args.assets/'license-audit'/(slug+'.json'),json_bytes(metadata))
        archive=args.assets/'downloads'/(slug+'.zip')
        if archive.exists():
            data=archive.read_bytes()
            previous=row.get('archive_sha256')
            if previous and hashlib.sha256(data).hexdigest()!=previous:
                raise ValueError('Cached archive hash mismatch: '+slug)
        else:
            request=urllib.request.Request(api+'/download',headers={'Authorization':'Token '+token})
            with urllib.request.build_opener(NoRedirect).open(request,timeout=45) as response:
                available=json.load(response)
            if 'gltf' not in available:
                raise ValueError('Official glTF download is unavailable: '+slug)
            # Never forward the API token to the asset CDN or save signed URLs.
            with urllib.request.urlopen(available['gltf']['url'],timeout=120) as response:
                data=response.read()
            atomic(archive,data)
        directory=args.assets/'source'/slug
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            if sum(i.file_size for i in z.infolist())>1024**3:
                raise ValueError('Archive expands beyond 1 GiB: '+slug)
            for member in z.infolist():
                path=directory/member.filename
                if not path.resolve().is_relative_to(directory.resolve()) or ((member.external_attr>>16)&0o170000)==0o120000:
                    raise ValueError('Unsafe archive member: '+member.filename)
                if not member.is_dir():
                    atomic(path,z.read(member))
        gltfs=sorted(directory.rglob('*.gltf'))+sorted(directory.rglob('*.glb'))
        if len(gltfs)!=1:
            raise ValueError(f'Expected one glTF scene in {slug}; found {len(gltfs)}')
        notices=[]
        for path in sorted(directory.rglob('*')):
            if path.is_file() and any(word in path.name.lower() for word in ('license','licence','credit','readme')):
                notices.append(str(path.relative_to(args.assets)))
                if path.stat().st_size<100000:
                    atomic(args.assets/'license-audit'/(slug+'-'+path.name),path.read_bytes())
        row.update(download_status=200,archive_path=str(archive.relative_to(args.assets)),archive_bytes=len(data),
                   archive_sha256=hashlib.sha256(data).hexdigest(),scene_path=str(gltfs[0].relative_to(args.assets)),
                   bundled_notices=notices)
        print(f"{slug}: downloaded {len(data):,} bytes, {len(notices)} license/credit files",flush=True)
        return row

    with ThreadPoolExecutor(max_workers=3) as pool:
        manifest['models']=list(pool.map(download,manifest['models']))
    manifest['download_status']='Official glTF archives downloaded; see per-model hashes and bundled notices.'
    atomic(manifest_path,json_bytes(manifest))


if __name__=='__main__':
    main()
