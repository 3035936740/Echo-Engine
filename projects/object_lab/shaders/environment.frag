#version 120
// Custom background ABI: all uniforms are visual. Never feed shader output to gameplay.
uniform vec2 u_resolution,u_camera;
uniform float u_time,u_intensity,u_wind,u_fog,u_generation;
uniform int u_environment,u_weather,u_sunlight;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+1.),f.x),f.y);}
void main(){
    vec2 uv=gl_FragCoord.xy/u_resolution;
    vec2 p=gl_FragCoord.xy;float t=u_time;
    vec3 top=vec3(.025,.045,.1),bottom=vec3(.13,.18,.25);
    if(u_environment==0){top=vec3(.16,.16,.31);bottom=vec3(.97,.63,.45);}
    if(u_environment==1){top=vec3(.17,.45,.68);bottom=vec3(.65,.78,.74);}
    if(u_environment==2){top=vec3(.2,.12,.26);bottom=vec3(.93,.42,.28);}
    if(u_environment==4){top=vec3(.015,.025,.09);bottom=vec3(.12,.13,.24);}
    if(u_environment==5){top=vec3(.015,.04,.045);bottom=vec3(.04,.12,.13);}
    vec3 color=mix(bottom,top,smoothstep(0.,1.,uv.y));
    float sun=length((uv-vec2(.72,u_environment==1?.76:.46))*vec2(1.78,1));
    if(u_environment<=2&&u_sunlight!=0){
        color+=vec3(.35,.18,.07)*exp(-sun*7.);
        color=mix(color,vec3(1.,.89,.64),1.-smoothstep(.052,.061,sun));
    }
    if((u_environment==3||u_environment==4||u_environment==6)&&u_sunlight!=0){
        float moon=length((uv-vec2(.77,.78))*vec2(1.78,1.));
        float cut=length((uv-vec2(.785,.79))*vec2(1.78,1.));
        color+=vec3(.55,.65,.71)*(1.-smoothstep(.028,.034,moon))*smoothstep(.025,.03,cut);
    }
    if(u_environment==3||u_environment==4||u_environment==6){
        vec2 cells=floor(p/3.);float h=hash(cells);
        float star=step(.982,h)*step(.79,hash(cells+19.))*smoothstep(.5,.9,uv.y);
        color+=vec3(star*(.45+.3*sin(t*1.2+h*70.)));
        if(u_environment==4){
            float cycle=floor(t/5.);float progress=fract(t/5.);
            vec2 head=vec2(1.1-progress*1.35,.93-progress*.55);
            vec2 delta=(uv-head)*vec2(1.78,1);
            float line=abs(delta.y-.4*delta.x);
            float meteor=exp(-line*350.)*smoothstep(-.2,0.,delta.x)*smoothstep(.35,0.,delta.x)*sin(progress*3.14159);
            color+=vec3(.45,.62,.85)*meteor*(.5+hash(vec2(cycle,u_generation)));
        }
    }
    if(u_environment==6){float ribbon=abs(uv.y-.6-sin(uv.x*6.+t*.12)*.1);color+=vec3(.1,.55,.36)*exp(-ribbon*18.)*(.6+.4*noise(uv*8.+t*.1));}
    // Layered silhouettes and parallax; every sample is evaluated on the virtual canvas.
    for(int i=0;i<3;i++){
        float layer=float(i);float wx=uv.x+u_camera.x*(.00003+layer*.00002);
        float hill=.21+layer*.085+sin(wx*(6.+layer*3.)+layer)*(.055+layer*.015)+noise(vec2(wx*8.,layer))*.08;
        if(uv.y<hill)color=mix(color,vec3(.025,.055,.07)+layer*vec3(.014,.024,.028),.85);
    }
    float cloud=noise(vec2(uv.x*5.+t*.02*u_wind,uv.y*9.))*noise(vec2(uv.x*14.,uv.y*10.));
    color+=vec3(.09)*smoothstep(.26,.6,cloud)*smoothstep(.4,.7,uv.y);
    if(u_weather==1){
        for(int layer=0;layer<2;layer++){
            float l=float(layer);vec2 rain=p*vec2(.07,.025)+vec2(-t*(1.+abs(u_wind))*2.,t*(9.+l*5.));
            rain.x+=p.y*.015*u_wind;
            vec2 cell=floor(rain);vec2 f=fract(rain);
            float amount=step(hash(cell),u_intensity*.7)*step(f.x,.05+l*.05)*smoothstep(.8,.3,f.y);
            color+=vec3(.22,.33,.4)*amount*(.7+l*.3);
        }
        color*=1.-u_intensity*.23;
        if(u_intensity>.95){float flash=pow(max(0.,sin(t*.7)*sin(t*3.7)),55.);color+=vec3(.65,.72,.8)*flash;}
    }
    if(u_weather==2){
        for(int layer=0;layer<3;layer++){
            float l=float(layer);vec2 snow=p/(10.-l*2.)+vec2(t*.12*u_wind,t*(.5+l*.4));
            snow.x+=sin(snow.y*.6+t*.4)*.4;
            vec2 cell=floor(snow),f=fract(snow)-.5;float radius=.08+l*.035;
            float flake=step(hash(cell),u_intensity*.65)*smoothstep(radius+.04,radius,length(f));
            color+=vec3(.75,.82,.84)*flake;
        }
    }
    float fog=clamp(u_fog,0.,1.)*(.5+.5*noise(uv*5.+vec2(t*.015,0.)))*smoothstep(.8,.1,uv.y);
    color=mix(color,vec3(.45,.57,.6),fog);
    // Gentle discrete tonal bands preserve pixel-art background detail.
    gl_FragColor=vec4(floor(clamp(color,0.,1.)*48.)/48.,1);
}
