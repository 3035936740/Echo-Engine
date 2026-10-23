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
