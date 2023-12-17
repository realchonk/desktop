
// Define the color steps.
static const struct MyColor steps[] = {
	{ 1.0, 0.0, 0.0 },
	{ 0.0, 1.0, 0.0 },
	{ 0.0, 0.0, 1.0 },
};

// The initial background color.
static const struct MyColor initial_color = { 0.5, 0.5, 0.5 };

// Interpolation increent per substep.
static const float sub_step_inc = 0.01f;

// Delay (in milliseconds) between substeps.
static const unsigned delay = 100;
