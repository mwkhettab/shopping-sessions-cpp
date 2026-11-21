#![allow(dead_code)]
mod keyrec;
use keyrec::*;
use std::io::{self, Write};
fn main(){
    let args:Vec<_>=std::env::args().collect();
    let mut out=io::BufWriter::new(io::stdout().lock());
    let mut word=|x:u64|out.write_all(&x.to_le_bytes()).unwrap();
    let mut state=67842u64;
    for i in 0..100000{
        state=state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let(v,ok)=base36_word(state,i%8+1);
        word(v);word(ok as u64);word(scramble(state,1923888191));word(unscramble(state,900712138));
        let mut b=Vec::new();base36_encode(state&MASK,&mut b);word(base36_decode_swar(&b).unwrap_or(u64::MAX));
    }
    let mut s=Sample::default();
    s.add_event_lines(&std::fs::read(&args[1]).unwrap(),17000);
    s.add_click_lines(&std::fs::read(&args[2]).unwrap(),12000);
    word(s.events.len() as u64);word(s.clicks.len() as u64);
    for r in &s.events{word(r.id_x);word(r.user_x.unwrap_or(u64::MAX));word(r.ts.unwrap_or(i64::MIN) as u64);}
    for r in &s.clicks{word(r.id_x);word(r.user_x.unwrap_or(u64::MAX));}
    let mut stats=Stats::default();let k=recover(&s,None,&mut stats);
    word(k.map_or(u64::MAX,|k|k.user));word(k.map_or(u64::MAX,|k|k.event));word(k.map_or(u64::MAX,|k|k.click));
    word(stats.user_candidates_tried);word(stats.event_steps);word(stats.click_steps);
}
