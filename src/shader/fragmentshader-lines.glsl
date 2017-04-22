uniform float stripDepth;
uniform float stripWidth;
uniform float strokeWidth;
uniform float strokeDepthInfluence;

uniform sampler2D strokeTexture;
uniform sampler2D stripMaskTexture;

float getStroke(float x, float y, float width)
{
	float tc = ((0.5*x*stripWidth)/width - 0.5);
	vec4 returnStroke = vec4(texture2D(strokeTexture,vec2(y,tc)));
	return returnStroke.x;
}

float getDepthDisplacement(float x)
{
	if (x < 0.3) {
		return 0.0;	
	} else {
		return 1.0;
	} 
}

float getDepthDisplacement2(float x)
{
	return x;
}


void main(void)
{
	float stripMask = (texture2D(stripMaskTexture,gl_TexCoord[0].xy)).x;
	if (stripMask < 0.5) {
		discard; // tapered line ends
	}
	float side = abs(2.0*(gl_TexCoord[0].y - 0.5));
	float stroke = getStroke(side,gl_TexCoord[0].x,strokeWidth* ((1.0-strokeDepthInfluence) + strokeDepthInfluence*(1.0-(gl_FragCoord.z-0.48)/0.24)));
	float displacement = 0.0;

    if (stroke > 0.0) {
		displacement = getDepthDisplacement2(side)*stripDepth;
	}
	gl_FragDepth = gl_FragCoord.z + displacement;
	gl_FragColor = vec4(stroke,stroke,stroke,1.0);
}