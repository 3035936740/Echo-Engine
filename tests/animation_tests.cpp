#include "echo/animations.hpp"
#include "echo/objects.hpp"
#include "echo/simulation.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cmath>
using namespace echo;
static void check(bool ok,const char* s) { if(!ok) throw std::runtime_error(s); }
template<class F> static void rejects(F f,const char* s) {
    bool no=false; try { f(); } catch(...) {no=true;} check(no,s);
}
static void fresh(WorldState& w) {
    w=makeWorld(); w.aster.enabled=false; w.kino.invulnTimer=1000;
    w.audio.lengths[0]=10; w.level.objects.clear();
}
int main() {
    try {
        AnimationLibrary library;
        AnimationClip clip; clip.id=allocateAnimationId(library); clip.name.set("Attack");
        clip.sprite.set("assets/attack.png"); clip.loop=false; clip.frameWidth=clip.frameHeight=32;
        AnimationFrame f; f.seconds=.02f; f.collision={3,4,20,40,true};
        f.hurt={11,28,6,6,true}; f.variable=0; f.delta=1; f.sound=0;
        clip.frames.push_back(f); f.column=1; f.seconds=1.f/240;
        f.attack={0,0,120,48,true}; f.damage=5; f.delta=10; clip.frames.push_back(f);
        f.column=2; f.attack.enabled=false; f.delta=100; f.seconds=.02f; clip.frames.push_back(f);
        library.clips.push_back(clip); library.player.fill(clip.id);
        auto text=saveAnimations(library);
        check(saveAnimations(loadAnimations(text))==text,"Animation format roundtrip");
        auto next=loadAnimations(text); next.clips.clear();
        check(allocateAnimationId(next)>clip.id,"Deleted animation identity is never reused");
        rejects([&]{loadAnimations(text+"junk");},"Trailing data rejected");
        auto invalid=library; invalid.clips[0].frames[0].seconds=0;
        rejects([&]{saveAnimations(invalid);},"Zero duration rejected");
        invalid=library; invalid.player[0]=99;
        rejects([&]{saveAnimations(invalid);},"Missing binding rejected");
        invalid=library; invalid.clips[0].frames[0].collision.w=3000;
        rejects([&]{saveAnimations(invalid);},"Oversized collision rejected");
        auto w=std::make_unique<WorldState>(); fresh(*w);
        w->kino.x=100; w->kino.y=300;
        auto enemy=objectPreset(1,0); enemy.damage=0; enemy.gravity=false; enemy.hp=20; enemy.speed=0;
        auto h=createObject(*w,enemy,{180,300},0);
        stepAnimations(*w,library,0);
        check(w->events.variables[0]==1 && w->audio.voices.count==1,"First-frame marker once");
        stepAnimations(*w,library,0);
        check(w->events.variables[0]==1 && w->audio.voices.count==1,"Paused / zero-dt never repeats marker");
        auto before=std::make_unique<WorldState>(*w);
        stepAnimations(*w,library,.03f);
        check(w->kino.animation.frame==2 && w->events.variables[0]==111,"Every crossed frame executes");
        check(resolveObject(*w,h)->hp==15,"Short attack frame cannot be skipped");
        auto after=w->kino.animation; auto voices=w->audio.voices.count;
        stepAnimations(*w,library,0);
        check(resolveObject(*w,h)->hp==15 && w->audio.voices.count==voices,"No repeated damage on paused frame");
        restore(*w,*before); stepAnimations(*w,library,.03f);
        check(w->kino.animation==after && resolveObject(*w,h)->hp==15 &&
              w->events.variables[0]==111 && w->audio.voices.count==voices,
              "Restore across attack marker reproduces the same branch");
        restore(*w,*before); w->kino.x=160; w->kino.y=592;
        w->level.tiles[18*mapWidth+6]=Tile::Solid;
        for(int i=0;i<90;++i) stepPhysics(*w,{1},fixedDt);
        check(w->kino.x+3+20<=192.1f,"Frame collision offset blocks at correct sprite origin");
        auto origin=w->kino.x;
        auto anchored=library; anchored.clips[0].anchorX=20;
        stepAnimations(*w,anchored,0,false);
        check(w->kino.x==origin,"Visual anchor never changes WorldState position");
        auto body=kinoBody(*w);
        check(std::abs(body.x-(w->kino.x+3))<.01f && body.w==20,"Collision dimensions are gameplay");
        fresh(*w);
        w->kino.animation={}; w->kino.animation.collision={3,4,20,40,true};
        w->kino.facing=-1;
        check(std::abs(kinoBody(*w).x-(w->kino.x+5))<.01f,"Facing mirrors frame boxes");
        fresh(*w);
        auto player=objectPreset(2,7); player.solid=true; player.width=40; player.height=60; player.hp=9;
        auto ph=createObject(*w,player,{160,560},0);
        check(w->kino.width==40 && w->kino.height==60 && w->kino.hp==9,"Player template updates authoritative body");
        for(int i=0;i<60;++i) stepPhysics(*w,{1},fixedDt);
        check(w->kino.x>200,"Player solid object never collides with itself");
        check(requestAnimation(*w,library,ph,clip.id),"Object handle can request player animation");
        stepAnimations(*w,library,0,false);
        check(w->kino.animation.requested==clip.id,"Manual playback request is in snapshot");
        destroyObject(*w,ph);
        check(!requestAnimation(*w,library,ph,clip.id),"Stale handle cannot control playback");
        auto loop=library; loop.clips[0].loop=true;
        for(int hz:{30,60,120}) {
            fresh(*w); w->rewind.record_hz=hz;
            Simulation sim(16ull*1024*1024); sim.setAnimations(loop); sim.reset(*w);
            for(int i=0;i<200;++i) sim.tick({});
            auto expected=sim.history().at(0).kino.animation;
            auto marker=sim.history().at(0).events.variables[0];
            sim.seekHistory(0);
            check(sim.world().kino.animation==expected && sim.world().events.variables[0]==marker,
                  "Oldest history restores frame and marker cursor");
            auto restored=std::make_unique<WorldState>(sim.world());
            auto playback=restored->kino.animation;
            advanceAnimation(*restored,playback,loop,clip.id,fixedDt);
            sim.tick({}); sim.tick({});
            check(sim.world().state==TimeState::Free,"Continue after farthest animation rewind");
        }
        std::cout<<"PASS animation frames, markers, short attacks, handles, boxes and farthest rewind\nWorldState "
                 <<sizeof(WorldState)<<" bytes\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
