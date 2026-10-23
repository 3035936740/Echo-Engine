#pragma once
namespace echo {
inline constexpr const char* samplingShader = R"GLSL(
#version 120
uniform sampler2D u_image;
uniform vec2 u_size;
uniform int u_mode;
uniform float u_sharpness;
uniform vec4 u_bounds;
uniform vec2 u_cell;
vec4 fetch(vec2 pixel) {
 vec2 low=u_bounds.xy,high=u_bounds.zw;
 if(u_cell.x>0.) {vec2 base=floor(clamp(gl_TexCoord[0].xy*u_size,vec2(0),u_size-.001)/u_cell)*u_cell;low=max(low,(base+.5)/u_size);high=min(high,(base+u_cell-.5)/u_size);}
 vec2 uv=clamp((pixel+.5)/u_size,low,high);
 return texture2D(u_image,uv);
}
vec4 pm(vec4 v) {return vec4(v.rgb*v.a,v.a);}
float sinc(float x) {if(abs(x)<.0001)return 1.;x*=3.14159265359;return sin(x)/x;}
float cubic(float x) {x=abs(x);if(x<1.)return 1.5*x*x*x-2.5*x*x+1.;if(x<2.)return -.5*x*x*x+2.5*x*x-4.*x+2.;return 0.;}
void main() {
 vec2 p=gl_TexCoord[0].xy*u_size-.5,base=floor(p),f=fract(p);
 vec4 value;
 if(u_mode==0) value=fetch(floor(p+.5));
 else if(u_mode==6) {
   vec4 a=pm(fetch(base)),b=pm(fetch(base+vec2(1,0))),c=pm(fetch(base+vec2(0,1))),d=pm(fetch(base+1.));
   value=f.x+f.y<1. ? a+f.x*(b-a)+f.y*(c-a) : d+(1.-f.x)*(c-d)+(1.-f.y)*(b-d);
 } else if(u_mode<=2) {
   if(u_mode==2) {float scale=max(u_sharpness*8.,1.);f=clamp((f-.5)*scale+.5,0.,1.);}
   value=mix(mix(pm(fetch(base)),pm(fetch(base+vec2(1,0))),f.x),mix(pm(fetch(base+vec2(0,1))),pm(fetch(base+1.)),f.x),f.y);
 } else {
   float radius=u_mode==5?3.:2.;value=vec4(0);float total=0.;
   for(int y=-2;y<=3;++y)for(int x=-2;x<=3;++x) {
     vec2 d=vec2(float(x),float(y))-f;
     float wx=u_mode==3?cubic(d.x):(abs(d.x)<radius?sinc(d.x)*sinc(d.x/radius):0.);
     float wy=u_mode==3?cubic(d.y):(abs(d.y)<radius?sinc(d.y)*sinc(d.y/radius):0.);
     float weight=wx*wy;vec4 c=fetch(base+vec2(float(x),float(y)));
     c.rgb*=c.a;value+=c*weight;total+=weight;
   }
   value/=max(total,.00001);if(value.a>.00001)value.rgb/=value.a;
   value=clamp(value,0.,1.);
 }
 if(u_mode==1||u_mode==2||u_mode==6) {if(value.a>.00001)value.rgb/=value.a;else value.rgb=vec3(0);}
 gl_FragColor=value*gl_Color;
}
)GLSL";
inline constexpr const char* reductionShader=R"GLSL(
#version 120
uniform sampler2D u_image;
uniform int u_factor;
void main() {
 vec2 uv=gl_TexCoord[0].xy,base=floor(uv*vec2(320,180));
 vec4 color=vec4(0);
 for(int y=0;y<4;++y)for(int x=0;x<4;++x)if(x<u_factor && y<u_factor) {
   vec2 sampleUV=(base+(vec2(float(x),float(y))+.5)/float(u_factor))/vec2(320,180);
   color+=texture2D(u_image,sampleUV);
 }
 gl_FragColor=color/float(u_factor*u_factor);
}
)GLSL";
inline constexpr const char* fxaaShader=R"GLSL(
#version 120
uniform sampler2D u_image;
uniform int u_quality;
float luma(vec3 v) {return dot(v,vec3(.299,.587,.114));}
void main() {
 vec2 uv=gl_TexCoord[0].xy,stepUV=1./vec2(320,180);
 vec3 nw=texture2D(u_image,uv+vec2(-1,1)*stepUV).rgb;
 vec3 ne=texture2D(u_image,uv+vec2(1,1)*stepUV).rgb;
 vec3 sw=texture2D(u_image,uv+vec2(-1,-1)*stepUV).rgb;
 vec3 se=texture2D(u_image,uv+vec2(1,-1)*stepUV).rgb;
 vec4 m=texture2D(u_image,uv);
 float lo=min(luma(m.rgb),min(min(luma(nw),luma(ne)),min(luma(sw),luma(se))));
 float hi=max(luma(m.rgb),max(max(luma(nw),luma(ne)),max(luma(sw),luma(se))));
 if(hi-lo<max(.0312,hi*.125)) {gl_FragColor=m;return;}
 vec2 dir=vec2(-((luma(nw)+luma(ne))-(luma(sw)+luma(se))),((luma(nw)+luma(sw))-(luma(ne)+luma(se))));
 float reduce=max((luma(nw)+luma(ne)+luma(sw)+luma(se))*.03125,.0078125);
 float span=u_quality==1?4.:8.;
 dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),-span,span)*stepUV;
 vec3 a=.5*(texture2D(u_image,uv+dir*(-1./6.)).rgb+texture2D(u_image,uv+dir*(1./6.)).rgb);
 vec3 b=.5*a+.25*(texture2D(u_image,uv-dir*.5).rgb+texture2D(u_image,uv+dir*.5).rgb);
 gl_FragColor=vec4(u_quality==1 || luma(b)<lo || luma(b)>hi?a:b,m.a);
}
)GLSL";
}
