fn main() {
    let args:Vec<_>=std::env::args().collect();
    let folder=Path::new(&args[1]);
    let n:usize=args[2].parse().unwrap();
    let (mut bytes,mut off)=(Vec::new(),vec![0u32]);
    for j in 0..1031 {
        let s=if j%7==0 {String::new()} else {format!("s{j}{}","x".repeat(j%23))};
        bytes.extend(s.as_bytes());off.push(bytes.len() as u32);
    }
    let dict=StrCol{offsets:&off,data:&bytes};
    let select8:Vec<_>=(0..n).map(|i|if i%5!=0 {(i%21) as u8}else{255}).collect();
    let select16:Vec<_>=(0..n).map(|i|if i%12!=0 {((i*577)%1031) as u16}else{65535}).collect();
    let select32:Vec<_>=(0..n).map(|i|if i%7!=0 {(i%127) as u32}else{u32::MAX}).collect();
    let global:Vec<_>=(0..n).map(|i|97+(i%37) as u32).collect();
    let dense:Vec<_>=(0..n).map(|i|(i%97) as i32-40).collect();
    let spread:Vec<_>=(0..n).map(|i|((i*10007)&0x7fffffff) as i32).collect();
    let epoch:Vec<_>=(0..n).map(|i|20000+(i%14) as i32).collect();
    let hash:Vec<_>=(0..n).map(|i|((i*577)%1021) as i64*1000000001-4101).collect();
    let extreme:Vec<_>=(0..n).map(|i|if i%2!=0{i64::MAX}else{i64::MIN}).collect();
    let nulls=vec![255u8;n];
    let (mut string,mut offsets)=(Vec::new(),vec![0u32]);
    for i in 0..n {
        for j in 0..i%18 {string.push(97+((i+j)%26) as u8);}
        offsets.push(string.len() as u32);
    }
    let c=[
      Column{name:"dense",data:Data::I32(&dense,IntEnc::Dense)},
      Column{name:"spread",data:Data::I32(&spread,IntEnc::Auto)},
      Column{name:"hash",data:Data::I64(&hash,IntEnc::Hash)},
      Column{name:"extreme",data:Data::I64(&extreme,IntEnc::Auto)},
      Column{name:"day",data:Data::Date(&epoch,IntEnc::Dense)},
      Column{name:"str",data:Data::Str(StrCol{offsets:&offsets,data:&string})},
      Column{name:"small",data:Data::Dict{dict,keys:Keys::U8(&select8),nullable:true,plain:false}},
      Column{name:"wide",data:Data::Dict{dict,keys:Keys::U16(&select16),nullable:true,plain:false}},
      Column{name:"plain",data:Data::Dict{dict,keys:Keys::U32(&select32),nullable:true,plain:true}},
      Column{name:"global",data:Data::Dict{dict,keys:Keys::U32Off(&global,91),nullable:false,plain:false}},
      Column{name:"nil",data:Data::Dict{dict,keys:Keys::U8(&nulls),nullable:true,plain:false}}];
    let opts=Opts{rows_per_group:8197};
    std::fs::write(folder.join("reference.parquet"),encode(&c,&opts)).unwrap();
    let mut o=Out::mem(0);
    for w in 1..=32u32 {for n in [1,7,8,9,8191,8192,8193] {
        let mask=u32::MAX>>(32-w);
        unsafe{emit_packed(&mut o,n,w,|i| (i*97813+21) as u32&mask).unwrap();}
    }}
    std::fs::write(folder.join("reference.bin"),o.buf).unwrap();
}
