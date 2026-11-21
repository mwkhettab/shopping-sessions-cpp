#![allow(dead_code)]
mod session;
mod keyrec;
mod parse { pub const SKIP:u32=0; }
mod sys {
    use std::sync::atomic::AtomicBool;
    pub static CORE_NOTOUCH:AtomicBool=AtomicBool::new(true);
    pub fn anon(n:usize)->*mut u8 {
        unsafe{std::alloc::alloc_zeroed(std::alloc::Layout::from_size_align(n.max(1),4096).unwrap())}
    }
    pub fn fallback(why:&str)->! { panic!("{}",why); }
}
include!("names.rs");
pub const WEEK_MS:u64=604800000;
pub const GAP_MS:u64=1800000;
pub const DAY_MS:i64=86400000;
pub const CBOUND:usize=1<<14;
pub struct G { ukey:u64 }
pub struct Rec { w0:u64 }
fn main() {
    use std::io::{Read,Write};
    let mut text=String::new();std::io::stdin().read_to_string(&mut text).unwrap();
    let mut words=text.split_whitespace().map(|s|s.parse::<u64>().unwrap());
    let mut get=||words.next().unwrap();
    let mut bytes=Vec::<u8>::new(); let mut raw=|x:u64|bytes.extend(x.to_le_bytes());
    let g=G{ukey:get()};let nc=get() as usize;let nd=get() as usize;let ws=get();
    let day=get() as i32;let np=get();
    let mut c=session::Core::new(nc,nd,20000,ws,day);
    for _ in 0..np {
        let mode=get();let u=get() as u32;let mask=get() as u32;let ne=get() as usize;let nk=get() as usize;
        let evs=(0..ne).map(|_|Rec{w0:get()}).collect::<Vec<_>>();
        let cks=(0..nk).map(|_|[get(),get()]).collect::<Vec<_>>();
        let name=if mask==0 {session::Name::Bytes(&[65,0,111,100,100,255])} else {session::Name::Simple(u,mask)};
        if mode==0 {c.person(&g,&evs,&cks,name);continue}
        let mut st=c.begin(evs[0].w0);
        if mode==1 || mode==3 {
            let v=evs.iter().map(|r|r.w0).collect::<Vec<_>>();
            let (bits,uns)=c.feed_slice(&mut st,&v,v[0]>>33);
            raw(bits as u64);raw(uns as u64);
            if mode==3{c.abort(&st);continue}
        }else if mode==2{
            let mut series=||{let n=get() as usize;(0..n).map(|_|get()).collect::<Vec<_>>()};
            let se=series();let pu=series();let ex=series();let mut xi=0;
            raw(c.feed_digest(&mut st,&se,&pu,&ex,&mut xi) as u64);raw(xi as u64);
            for &x in &ex[xi..] {c.feed(&mut st,x);}
        }
        c.finish(&g,&st,&cks,name);
    }
    macro_rules! col {($x:expr)=>{raw($x.len() as u64);for &v in $x.as_slice(){raw(v as u64);}}}
    raw(c.n_sess);col!(c.names_off);col!(c.names);col!(c.keys);col!(c.day);
    col!(c.ses);col!(c.evs);col!(c.rev);col!(c.cat);
    for i in 0..c.agg.len(){for x in c.agg[i]{raw(x as u64)}for x in c.aggp[i]{raw(x)}}
    std::io::stdout().lock().write_all(&bytes).unwrap();
}
