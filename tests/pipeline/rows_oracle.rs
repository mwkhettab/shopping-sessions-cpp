#![allow(dead_code)]
mod iface;
mod k_ref;
use iface::*;
use std::io::{self, Write};

fn word(w: &mut impl Write, v:u64){w.write_all(&v.to_le_bytes()).unwrap();}
struct Sink<'a> { e:Box<EvBatch>, c:Box<CkBatch>, out: &'a mut io::BufWriter<io::Stdout> }
impl EventSink for Sink<'_> {
    fn ev(&mut self)->*mut EvBatch{&mut *self.e}
    fn ev_flush(&mut self){
        for i in 0..self.e.n{
            let r=self.e.row(i);
            for n in [self.e.pp[i],r.eid,r.uid,r.ts as u64,r.ingest as u64,r.kind as u64,r.product as u64,r.price as u64]{word(self.out,n)}
        }
        self.e.n=0;
    }
}
impl ClickSink for Sink<'_>{
    fn ck(&mut self)->*mut CkBatch{&mut *self.c}
    fn ck_flush(&mut self){
        for i in 0..self.c.n{
            let r=self.c.row(i);for n in [r.cid,r.uid,r.campaign as u64,r.ts as u64]{word(self.out,n)}
        }
        self.c.n=0;
    }
}
fn main(){
    let args:Vec<_>=std::env::args().collect();
    let buf=std::fs::read(&args[2]).unwrap();
    let mut out=io::BufWriter::new(io::stdout());
    if args[1]=="events" {
        k_ref::events_with(&buf,|r,a,b|for n in [a as u64,b as u64,r.eid,r.uid,r.ts as u64,r.ingest as u64,r.kind as u64,r.product as u64,r.price as u64]{word(&mut out,n)});
        let mut sink=Sink{e:EvBatch::new(),c:CkBatch::new(),out:&mut out};
        k_ref::Ref::events(&buf,&mut sink);sink.ev_flush();
    }else{
        k_ref::clicks_with(&buf,|r,a,b|for n in [a as u64,b as u64,r.cid,r.uid,r.campaign as u64,r.ts as u64]{word(&mut out,n)});
        let mut sink=Sink{e:EvBatch::new(),c:CkBatch::new(),out:&mut out};
        k_ref::Ref::clicks(&buf,&mut sink);sink.ck_flush();
    }
}
