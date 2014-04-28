#ifdef FRAGMENT_SHADER

myhalf FragmentSoftness(float Depth, sampler2D DepthTexture, in vec2 ScreenCoord, in ivec4 Viewport, in vec2 ZRange, myhalf Scale)
{
	vec2 tc = ScreenCoord * u_TextureParams.zw;

	myhalf fragdepth = ZRange.x*ZRange.y/(ZRange.y - qf_texture(DepthTexture, tc).r*(ZRange.y-ZRange.x));
	myhalf partdepth = Depth;
	
	myhalf d = max((fragdepth - partdepth) * Scale, 0.0);
	myhalf softness = 1.0 - min(1.0, d);
	
	softness *= softness;
	softness = 1.0 - softness * softness;
	return softness;
}

#endif
