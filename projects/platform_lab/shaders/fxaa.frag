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
