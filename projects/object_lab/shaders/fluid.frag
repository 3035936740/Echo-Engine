#version 120
// Stable fluids: semi-Lagrangian advection, viscosity, divergence, Jacobi pressure,
// and pressure-gradient projection. All state is GPU visual state, outside snapshots.
uniform sampler2D u_velocity,u_pressure,u_divergence,u_dye;
uniform vec2 u_resolution,u_source,u_force;
uniform float u_dt;
uniform int u_pass;
vec2 uv(){return gl_FragCoord.xy/u_resolution;}
void main(){
    vec2 p=uv(),e=1./u_resolution;
    vec2 v=texture2D(u_velocity,p).xy;
    if(u_pass==0){
        vec2 old=p-v*u_dt/u_resolution;
        vec2 value=texture2D(u_velocity,clamp(old,e,1.-e)).xy*.995;
        value+=u_force*exp(-dot(p-u_source,p-u_source)*800.)*u_dt*12.;
        if(p.x<e.x||p.x>1.-e.x)value.x=0.;
        if(p.y<e.y||p.y>1.-e.y)value.y=0.;
        gl_FragColor=vec4(value,0,1);
    }else if(u_pass==1){
        float dx=texture2D(u_velocity,p+vec2(e.x,0)).x-texture2D(u_velocity,p-vec2(e.x,0)).x;
        float dy=texture2D(u_velocity,p+vec2(0,e.y)).y-texture2D(u_velocity,p-vec2(0,e.y)).y;
        gl_FragColor=vec4((dx+dy)*.5,0,0,1);
    }else if(u_pass==2){
        float sum=texture2D(u_pressure,p+vec2(e.x,0)).x+texture2D(u_pressure,p-vec2(e.x,0)).x+texture2D(u_pressure,p+vec2(0,e.y)).x+texture2D(u_pressure,p-vec2(0,e.y)).x;
        gl_FragColor=vec4((sum-texture2D(u_divergence,p).x)*.25,0,0,1);
    }else if(u_pass==3){
        vec2 grad=vec2(texture2D(u_pressure,p+vec2(e.x,0)).x-texture2D(u_pressure,p-vec2(e.x,0)).x,texture2D(u_pressure,p+vec2(0,e.y)).x-texture2D(u_pressure,p-vec2(0,e.y)).x)*.5;
        gl_FragColor=vec4(v-grad,0,1);
    }else if(u_pass==4){
        vec4 dye=texture2D(u_dye,clamp(p-v*u_dt/u_resolution,e,1.-e))*.987;
        float inject=exp(-dot(p-u_source,p-u_source)*1800.)*u_dt*2.;
        dye+=vec4(.23,.75,.62,1.)*inject;gl_FragColor=clamp(dye,0.,1.);
    }else{
        vec2 sum=texture2D(u_velocity,p+vec2(e.x,0)).xy+texture2D(u_velocity,p-vec2(e.x,0)).xy+texture2D(u_velocity,p+vec2(0,e.y)).xy+texture2D(u_velocity,p-vec2(0,e.y)).xy;
        gl_FragColor=vec4((v+sum*.08)/1.32,0,1);
    }
}
