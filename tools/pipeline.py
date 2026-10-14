#!/usr/bin/env python3
"""Extract, convert, rebuild, and verify indexed ARC archives.

All work directories are caller-selected. Source archives are read-only inputs.
"""
from __future__ import annotations
import argparse, hashlib, json, os, shutil, struct, subprocess, tempfile
from pathlib import Path
from PIL import Image
import numpy as np

MAGIC_EXT = {".png"}

def sha256(p):
 h=hashlib.sha256()
 with open(p,"rb") as f:
  for b in iter(lambda:f.read(1<<20),b""): h.update(b)
 return h.hexdigest()

def decode_name(raw):
 n=len(raw); return bytes((b+(n-i))&255 for i,b in enumerate(raw)).decode("ascii")
def encode_name(name):
 raw=name.encode("ascii"); n=len(raw); return bytes((b-(n-i))&255 for i,b in enumerate(raw))

def read_index(arc):
 with open(arc,"rb") as f:
  index_size=struct.unpack("<I",f.read(4))[0]; records=[]; index_end=4+index_size
  while f.tell() < index_end:
   q=f.read(1);
   if len(q)!=1: raise ValueError("truncated name length")
   raw=f.read(q[0]); name=decode_name(raw)
   vals=f.read(12)
   if len(vals)!=12: raise ValueError("truncated record")
   original,stored,offset=struct.unpack(">III",vals)
   if stored!=original: raise ValueError(f"compressed ARC entry unsupported: {name}")
   if not name or "/" in name or "\\" in name or ".." in name: raise ValueError(f"unsafe name: {name}")
   records.append({"name":name,"original":original,"stored":stored,"offset":offset})
  if f.tell() != index_end: raise ValueError("ARC index boundary mismatch")
 return records

def extract(arc,out):
 out=Path(out); out.mkdir(parents=True,exist_ok=True); records=read_index(arc)
 with open(arc,"rb") as f:
  for r in records:
   f.seek(r["offset"]); data=f.read(r["stored"])
   if len(data)!=r["stored"]: raise ValueError("payload out of bounds")
   p=out/r["name"]; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data)
 return records

def pixels(p):
 with Image.open(p) as im:
  im.load(); mode="RGBA" if "A" in im.getbands() or "transparency" in im.info else "RGB"
  x=im.convert(mode); return x.size,mode,x.tobytes()

def convert(root,work,bin_dir,quality=90,effort=8,threads=6):
 root=Path(root); work=Path(work); work.mkdir(parents=True,exist_ok=True); bin_dir=Path(bin_dir)
 if threads<1 or threads>6: raise ValueError("threads must be 1..6")
 manifest={"quality":quality,"effort":effort,"threads":threads,"alpha_distance":0,"entries":{}}
 for src in sorted(root.rglob("*")):
  if not src.is_file(): continue
  rel=src.relative_to(root).as_posix(); ent={"source":rel,"source_sha256":sha256(src),"source_size":src.stat().st_size,"kind":"original","reason":"not eligible"}
  if src.suffix.lower()==".png" and not any(x in src.stem.lower() for x in ("font","rule","mask","label","thumb")):
   try:
    size,mode,raw=pixels(src)
    if mode in ("RGB","RGBA") and size[0]*size[1] <= 0x3fffffff:
     d=work/"items"/hashlib.sha256(rel.encode()).hexdigest(); d.mkdir(parents=True,exist_ok=True)
     jxl=d/"candidate.jxl"; decoded=d/"decoded.png"
     subprocess.run([str(bin_dir/"cjxl.exe"),str(src),str(jxl),"-q",str(quality),"-e",str(effort),"--alpha_distance=0",f"--num_threads={threads}","--quiet"],check=True)
     subprocess.run([str(bin_dir/"djxl.exe"),str(jxl),str(decoded),f"--num_threads={threads}","--bits_per_sample=8","--quiet"],check=True)
     ds,dm,dr=pixels(decoded)
     a=np.frombuffer(raw,dtype=np.uint8).reshape(-1,len(mode)); b=np.frombuffer(dr,dtype=np.uint8).reshape(-1,len(mode))
     alpha_equal=len(mode)!=4 or bool(np.array_equal(a[:,3],b[:,3]))
     if ds!=size or dm!=mode or not alpha_equal: raise ValueError("decoded geometry/mode/alpha mismatch")
     if jxl.stat().st_size < src.stat().st_size:
      ent.update({"kind":"jxl","reason":"JXL smaller","payload":str(jxl),"payload_sha256":sha256(jxl),"payload_size":jxl.stat().st_size,"width":size[0],"height":size[1],"mode":mode,"alpha_equal":alpha_equal})
     else: ent["reason"]="JXL not smaller"
   except Exception as e: ent["reason"]=f"preserved: {e}"
  manifest["entries"][rel]=ent
 (work/"manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
 return manifest

def build(root,archive,work,out):
 root=Path(root); work=Path(work); out=Path(out); records=read_index(archive); manifest=json.loads((work/"manifest.json").read_text(encoding="utf-8")); out.parent.mkdir(parents=True,exist_ok=True)
 body=bytearray(); rec=[]
 for r in records:
  e=manifest["entries"].get(r["name"],{}); src=root/r["name"]; payload=Path(e["payload"]) if e.get("kind")=="jxl" else src
  data=payload.read_bytes(); rec.append((r["name"],len(data),data))
 index_end=4+sum(1+len(encode_name(n))+12 for n,_,_ in rec)
 index=bytearray(struct.pack("<I",index_end-4))
 offset=index_end
 for name,size,data in rec:
  en=encode_name(name); index.append(len(en)); index.extend(en); index.extend(struct.pack(">III",size,size,offset)); body.extend(data); offset+=size
 out.write_bytes(bytes(index)+bytes(body)); return {"files":len(rec),"size":out.stat().st_size,"sha256":sha256(out)}

def verify(arc):
 p=Path(arc); rs=read_index(p); size=p.stat().st_size
 index_end=4+sum(1+len(encode_name(r["name"]))+12 for r in rs)
 if index_end>size: raise ValueError("truncated ARC index")
 for r in rs:
  if r["offset"]<index_end or r["offset"]+r["stored"]>size: raise ValueError("payload bounds invalid")
 return {"path":str(p.resolve()),"files":len(rs),"index_size":index_end,"size":size,"sha256":sha256(p)}

def main():
 ap=argparse.ArgumentParser(); sp=ap.add_subparsers(dest="cmd",required=True)
 x=sp.add_parser("extract"); x.add_argument("archive",type=Path); x.add_argument("out",type=Path)
 c=sp.add_parser("convert"); c.add_argument("root",type=Path); c.add_argument("work",type=Path); c.add_argument("--bin",type=Path,required=True); c.add_argument("--threads",type=int,default=6)
 b=sp.add_parser("build"); b.add_argument("root",type=Path); b.add_argument("archive",type=Path); b.add_argument("work",type=Path); b.add_argument("out",type=Path)
 v=sp.add_parser("verify"); v.add_argument("archive",type=Path)
 args=ap.parse_args()
 if args.cmd=="extract": print(json.dumps(extract(args.archive,args.out)))
 elif args.cmd=="convert": print(json.dumps(convert(args.root,args.work,args.bin,threads=args.threads),ensure_ascii=False))
 elif args.cmd=="build": print(json.dumps(build(args.root,args.archive,args.work,args.out)))
 else: print(json.dumps(verify(args.archive)))
if __name__=="__main__": main()
