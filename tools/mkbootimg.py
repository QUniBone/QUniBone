#!/usr/bin/env python3
# Build an RL02 image: boot block at sector 0, program (from address 1000) at
# sector 1 onward. Parses macro11 listings, converting PC-relative operands.
import re,sys
def parse(lst):
    mem={}
    for line in open(lst):
        # A directive with many values spills onto continuation lines that carry
        # the address but no source-line number, so the line number is optional
        # (1-5 digits, since the address is a padded 6-digit octal).
        m=re.match(r'\s*(?:\d{1,5}\s+)?([0-7]{6})\s+(.*)',line)
        if not m: continue
        a=int(m.group(1),8); rest=m.group(2)
        for tok in re.finditer(r'([0-7]{6}|[0-7]{3})',rest):
            t=tok.group(1)
            if rest[:tok.start()].strip() and not re.match(r'^[0-7\s]+$',rest[:tok.start()]): break
            if len(t)==6:
                v=int(t,8)
                if rest[tok.end():tok.end()+1]=="'": v=(v-(a+2))&0xFFFF
                mem[a]=v&0xFF; mem[a+1]=(v>>8)&0xFF; a+=2
            else:
                mem[a]=int(t,8)&0xFF; a+=1
    return mem
def blob(mem,lo):
    hi=max(mem); return bytes(mem.get(a,0) for a in range(lo,hi+1))
boot=parse(sys.argv[1]); prog=parse(sys.argv[2])
bb=blob(boot,0); pb=blob(prog,0o1000)
SEC=256; RL02=512*2*40*SEC
img=bytearray(RL02)
HDR=[0o240,0o407,0o6,0,0o12,0,0o240,0o240,0o174400,0o240,0o407,0,0o400,0,0,0,0,0]
for i,wv in enumerate(HDR): img[i*2]=wv&0xFF; img[i*2+1]=(wv>>8)&0xFF
ld=blob(boot,0o44); img[0o44:0o44+len(ld)]=ld
img[SEC:SEC+len(pb)]=pb
open(sys.argv[3],"wb").write(img)
print(f"boot {len(bb)}B, prog {len(pb)}B ({-(-len(pb)//SEC)} sectors), image {len(img)}B -> {sys.argv[3]}")
