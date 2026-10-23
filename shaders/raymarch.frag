#version 120
uniform vec2 u_resolution,u_camera;
uniform float u_time,u_generation;
float sdf(vec3 p){
    float pillars=length(mod(p.xz+2.,4.)-2.)-.55;
    float floorPlane=p.y+1.6;
    return min(pillars,floorPlane);
}
void main(){
    vec2 uv=(gl_FragCoord.xy/u_resolution)*2.-1.;
    vec3 ro=vec3(u_camera.x*.003,.4,-5.);
    vec3 rd=normalize(vec3(uv.x*1.78,uv.y*.7,1.5));
    float distance=0.;float hit=0.;
    for(int i=0;i<40;i++){vec3 p=ro+rd*distance;float d=sdf(p);if(d<.01){hit=1.;break;}distance+=d;if(distance>24.)break;}
    if(hit<.5){gl_FragColor=vec4(0);return;}
    vec3 p=ro+rd*distance;vec2 e=vec2(.015,0);
    vec3 n=normalize(vec3(sdf(p+e.xyy)-sdf(p-e.xyy),sdf(p+e.yxy)-sdf(p-e.yxy),sdf(p+e.yyx)-sdf(p-e.yyx)));
    vec3 reflected=reflect(rd,n);
    float glint=pow(max(0.,dot(reflected,normalize(vec3(sin(u_time*.3),.8,-1.)))),8.);
    float light=max(0.,dot(n,normalize(vec3(.4,1.,-.3))));
    vec3 color=vec3(.035,.12,.14)*(.5+light)+vec3(.18,.23,.2)*glint;
    gl_FragColor=vec4(color,clamp(1.-distance/30.,0.,.72));
}
