#version 120
// Visual compositor: no normal maps, no gameplay reads, deterministic fireflies.
uniform sampler2D u_scene;
uniform vec2 u_resolution,u_camera;
uniform float u_time,u_wind;
uniform int u_environment,u_sunlight,u_fireflies,u_light_count;
uniform vec4 u_lights[32],u_colors[32];
float hash(float p){return fract(sin(p*127.1+311.7)*43758.5453);}
void main(){
    vec2 uv=gl_FragCoord.xy/u_resolution,p=gl_FragCoord.xy;
    vec3 base=texture2D(u_scene,uv).rgb,energy=vec3(0.);
    if(u_sunlight!=0){
        vec3 ambient=vec3(.94,.97,1.);
        if(u_environment==0)ambient=vec3(1.,.91,.86);
        if(u_environment==1)ambient=vec3(1.,.99,.9);
        if(u_environment==2)ambient=vec3(1.,.85,.8);
        if(u_environment>=3)ambient=vec3(.75,.83,.99);
        if(u_environment==5)ambient=vec3(.69,.8,.84);
        base*=ambient;
        if(u_environment<=2){
            vec2 sun=vec2(.72,u_environment==1?.76:.46);
            vec2 d=(uv-sun)*vec2(1.78,1.);
            float glow=exp(-length(d)*9.);
            float ray=pow(max(0.,sin(atan(d.y,d.x)*18.+u_time*.018)),12.);
            float shaft=ray*exp(-length(d)*3.)*.09*smoothstep(.1,.65,uv.y);
            energy+=vec3(1.,.65,.3)*(glow*.14+shaft);
        }else if(u_environment!=5){
            float moon=exp(-length((uv-vec2(.77,.78))*vec2(1.78,1.))*8.);
            energy+=vec3(.26,.4,.72)*moon*.13;
        }
        for(int i=0;i<32;i++){
            if(i>=u_light_count)break;
            vec4 lamp=u_lights[i];
            float distance=length(p-lamp.xy)/max(1.,lamp.z);
            float falloff=pow(max(0.,1.-distance),2.);
            float pulse=1.+u_colors[i].a*sin(u_time*7.+float(i)*9.);
            energy+=u_colors[i].rgb*falloff*lamp.w*pulse;
        }
    }
    // World-space fireflies survive camera pans and reconstruct when time rewinds.
    if(u_fireflies!=0&&(u_environment==2||u_environment==3||u_environment==4||u_environment==6)){
        for(int i=0;i<24;i++){
            float seed=float(i)+1.;
            vec2 world=vec2(hash(seed)*768.,76.+hash(seed+28.)*78.);
            world+=vec2(sin(u_time*.43+seed)*5.,cos(u_time*.29+seed*1.7)*4.);
            vec2 point=vec2(world.x-u_camera.x*.25,180.-world.y+u_camera.y*.25);
            point=floor(point);
            float dist=length(p-point);
            float blink=.32+.68*pow(.5+.5*sin(u_time*1.2+seed*2.4),2.);
            energy+=vec3(.7,.92,.26)*(exp(-dist*.7)*.18+step(dist,1.1)*.8)*blink;
        }
    }
    gl_FragColor=vec4(clamp(base+vec3(1.)-exp(-energy),0.,1.),1.);
}
