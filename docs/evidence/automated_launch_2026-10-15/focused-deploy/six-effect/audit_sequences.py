from pathlib import Path
import hashlib,zlib,json
p=Path(__file__).parent/'nuc1-wal.bin';raw=p.read_bytes();rows=[];commands={}
u=lambda b,o,n:int.from_bytes(b[o:o+n],'little')
assert len(raw)%4096==0
for at in range(0,len(raw),4096):
    block=raw[at:at+4096]
    if not any(block):continue
    assert u(block,0,4)==0x32425756 and u(block,4,2)==2 and u(block,6,2)==0
    length=u(block,24,4);covered=bytearray(block[:32+length]);covered[28:32]=bytes(4)
    assert zlib.crc32(covered)==u(block,28,4)
    assert not any(block[32+length:]);payload=block[32:32+length];pos=0
    while pos<len(payload):
        size=u(payload,pos+8,4);f=payload[pos:pos+size];pos+=size
        assert f[:4]==b'V2BF' and u(f,4,2)==1 and u(f,6,2)==2 and u(f,12,4)+88==size
        assert f[16:48].hex()=='ba60fcdd2bd286d52cc927b861d589848e2cb583f3e404e0b6f547d4e8289479'
        assert u(f,48,4)==1 and not any(f[52:56])
        assert hashlib.sha256(b'velocita.v2.bound-frame.v1'+f[:-32]).digest()==f[-32:]
        row=dict(index=u(f,56,8),term=u(f,64,8),entry_type=u(f,76,4))
        assert row['entry_type']==0
        c=f[80:-32];assert c[:4]==b'V2KC' and u(c,8,2)==12 and u(c,4,4)+44==len(c)
        assert hashlib.sha256(c[8:-32]).digest()==c[-32:]
        row.update(kind=u(c,10,2),caller=u(c,12,8),sequence=u(c,20,8))
        key=(row['caller'],row['sequence'])
        if key in commands:assert commands[key]==c
        commands[key]=c;rows.append(row)
assert [r['index'] for r in rows]==list(range(1,8))
assert set(commands)=={(7,n) for n in range(1,7)}
assert [(r['sequence'],r['kind']) for r in rows]==[(1,6),(2,7),(2,7),(3,20),(4,21),(5,21),(6,23)]
report=dict(wal_sha256=hashlib.sha256(raw).hexdigest(),block_checksums_verified=True,bound_frame_digests_verified=True,command_digests_verified=True,identical_duplicate_sequence=2,rows=rows,operator_next_sequence=7,strategy_next_sequence=1,scope='Read-only current-generation WAL audit, corroborated by producer and voter applied index 7; not an authenticated remote read proof')
(p.parent/'sequence-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
