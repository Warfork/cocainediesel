models/objects/jumppad_base
{
	cull front
	{
		map $whiteimage
		rgbGen const 0.05 0.05 0.05
	}
}

models/objects/jumppad_top
{
	deformvertexes move 0 0 -2 sin 1 1 0 2
	cull front
	{
		map $whiteimage
		rgbGen const 0.60 1 0.1
	}
}

models/objects/spikes_base
{
	cull front
	fog
	{
		map $whiteimage
		rgbGen const 0.05 0.05 0.05
	}
}

models/objects/spikes_top
{
	cull front
	fog
	{
		map $whiteimage
		rgbGen const 1 0.35 0
	}
}
