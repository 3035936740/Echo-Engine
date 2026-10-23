-- 5 phases x 5 variants. The engine schedules generic volleys and warned regions.
-- All generated timings, aim locks, fired flags and pattern motion enter WorldState.
boss.maxHp=500
boss.hp=500
function on_boss_attack(phase, variant)
    local cx=boss.x+24
    local dir=player.x<cx and 1 or -1
    engine.zone{x=cx-32,y=584,w=64,h=56,start=1.2,["end"]=5.4,label="CROSS THE SEAL"}
    local function gate(x,gap,start,finish,brake)
        engine.zone{x=x,y=220,w=18,h=gap-220,start=start,["end"]=finish,brake=brake,label=brake and "R BRAKE" or "SEALED"}
        engine.zone{x=x,y=gap+92,w=18,h=640-gap-92,start=start,["end"]=finish,brake=brake,label="GAP"}
    end
    if phase==1 then
        engine.volley{at=1.4+variant*.16,pattern="fan",count=9+variant*2,speed=150+variant*10,angle=dir>0 and 0 or 180,aim=true}
        engine.volley{at=3.3,pattern="ring",count=16+variant*3,speed=105}
        gate(cx+dir*(180+variant*18),420+variant*12,3.5,6,false)
    elseif phase==2 then
        for i=0,2 do engine.volley{at=1.6+i*.75+variant*.12,pattern="fan",count=13,speed=210,aim=true} end
        gate(cx+dir*(160+variant*14),430,3.8,6,false)
    elseif phase==3 then
        gate(cx-dir*(100+variant*10),390,1.1,3.1,false)
        gate(cx+dir*(140+variant*10),480,3.2,5.8,false)
        engine.volley{at=2+variant*.12,pattern="wave",count=18+variant*3,speed=130}
    elseif phase==4 then
        for i=0,3 do if i~=variant%4 then gate(240+i*230,390+(i%2)*75,1.2+i*.35,3.5+i*.35,false) end end
        engine.zone{x=cx-dir*105,y=570,w=12,h=70,start=2,["end"]=5.1,brake=true,label="R BRAKE"}
        engine.volley{at=4,pattern="spiral",count=32+variant*3,speed=170}
    else
        engine.volley{at=1.2,pattern="ring",count=32+variant*4,speed=170}
        gate(cx+dir*145,430+variant*10,2.5,4.5,false)
        engine.volley{at=4.6,pattern="fan",count=21,speed=230,aim=true}
        engine.volley{at=5.8,pattern="formation",count=36+variant*4,speed=100}
    end
end
