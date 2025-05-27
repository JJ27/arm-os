#include "mandelbrot.h"

void notmain(void)
{
	volatile struct GPU *gpu;
	gpu_prepare(&gpu);

	// Copy the shader code onto our gpu.
	memcpy((void *)gpu->code, mandelbrotshader, sizeof gpu->code);

	// Initialize the uniforms - now we have multiple qpus
	// Some uniforms to consider: Resolution, max iters, num_qpus, i, anything else
	for (int i = 0; i < NUM_QPUS; i++)
	{
		gpu->unif[i][0] = RESOLUTION;
		gpu->unif[i][1] = hex_recip((float)RESOLUTION);
		gpu->unif[i][2] = MAX_ITERS;
		gpu->unif[i][3] = NUM_QPUS;
		gpu->unif[i][4] = i;
		gpu->unif[i][5] = GPU_BASE + (uint32_t)&gpu->output;
		gpu->unif_ptr[i] = GPU_BASE + (uint32_t)&gpu->unif[i];
	}

	// ZERO OUT THE GPU Output array
	for (int i = 0; i < 2 * RESOLUTION; i++)
	{
		for (int j = 0; j < 2 * RESOLUTION; j++)
		{
			gpu->output[i][j] = 0;
		}
	}

	// GPU execution
	printk("Running code on GPU...\n");
	int start_time = timer_get_usec();
	int iret = gpu_execute(gpu);
	int end_time = timer_get_usec();
	printk("DONE!\n");
	int gpu_time = end_time - start_time;


	// CPU execution
	printk("Running code on CPU...\n");
	uint32_t output_cmp[2 * RESOLUTION][2 * RESOLUTION];
	int cpu_time = 0;
	float recip = float_recip((float)RESOLUTION);
	start_time = timer_get_usec();

	// Use this as a guide for your GPU Kernel
	for (int i = 0; i < 2 * RESOLUTION; i++)
	{
		float y = -1.0f + (recip * (float)i);
		for (int j = 0; j < 2 * RESOLUTION; j++)
		{
			float x = -1.0f + (recip * (float)j);
			float u = 0.0;
			float v = 0.0;
			float u2 = u * u;
			float v2 = v * v;
			int k;
			for (k = 1; k < MAX_ITERS && (u2 + v2 < 4.0); k++)
			{
				v = 2 * u * v + y;
				u = u2 - v2 + x;
				u2 = u * u;
				v2 = v * v;
			}
			if (k >= MAX_ITERS)
			{
				output_cmp[i][j] = 1;
			}
			else
			{
				output_cmp[i][j] = 0;
			}
		}
	}
	end_time = timer_get_usec();
	cpu_time = end_time - start_time;

	printk("Time taken on CPU: %d us\n", cpu_time);
	printk("Time taken on GPU: %d us\n", gpu_time);

	printk("Speedup: %dx\n", cpu_time / gpu_time);

	// Debug: Print sample values from different parts of the CPU output
	printk("\nSample CPU output values (top-left):\n");
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 5; j++) {
			printk("%d ", output_cmp[i][j]);
		}
		printk("\n");
	}

	printk("\nSample CPU output values (center):\n");
	for (int i = RESOLUTION-2; i < RESOLUTION+3; i++) {
		for (int j = RESOLUTION-2; j < RESOLUTION+3; j++) {
			printk("%d ", output_cmp[i][j]);
		}
		printk("\n");
	}

	printk("\nSample CPU output values (bottom-right):\n");
	for (int i = 2*RESOLUTION-5; i < 2*RESOLUTION; i++) {
		for (int j = 2*RESOLUTION-5; j < 2*RESOLUTION; j++) {
			printk("%d ", output_cmp[i][j]);
		}
		printk("\n");
	}

	printk("%d\n", output_cmp[0][0]);
	unsigned char GPU_OUT[2*RESOLUTION][2*RESOLUTION];
	for (int i=0; i<2*RESOLUTION; i++) {
		for (int j=0; j<2*RESOLUTION; j++) {
			// Convert 1/0 to 255/0 for PGM format
			GPU_OUT[i][j] = gpu->output[i][j] * 255;
		}
	}

	// Debug: Print sample values from different parts of the final output
	printk("\nSample GPU_OUT values (top-left):\n");
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 5; j++) {
			printk("%d ", GPU_OUT[i][j]);
		}
		printk("\n");
	}

	printk("\nSample GPU_OUT values (center):\n");
	for (int i = RESOLUTION-2; i < RESOLUTION+3; i++) {
		for (int j = RESOLUTION-2; j < RESOLUTION+3; j++) {
			printk("%d ", GPU_OUT[i][j]);
		}
		printk("\n");
	}

	printk("\nSample GPU_OUT values (bottom-right):\n");
	for (int i = 2*RESOLUTION-5; i < 2*RESOLUTION; i++) {
		for (int j = 2*RESOLUTION-5; j < 2*RESOLUTION; j++) {
			printk("%d ", GPU_OUT[i][j]);
		}
		printk("\n");
	}

	// Print debug values from VPM
	for (int i = 0; i < NUM_QPUS; i++) {
		printk("Debug values for QPU %d:\n", i);
		// Read debug values from the first row of output
		float y_coord = *((float*)&gpu->output[i][0]);
		float x_coord = *((float*)&gpu->output[i][1]);
		float div_check = *((float*)&gpu->output[i][2]);
		int iter_count = gpu->output[i][3];
		printk("y-coordinate: %f\n", y_coord);
		printk("x-coordinate: %f\n", x_coord);
		printk("divergence check: %f\n", div_check);
		printk("iterations used: %d\n", iter_count);
		printk("\n");
	}

	kmalloc_init(8*FAT32_HEAP_MB);
  	pi_sd_init();

	mbr_t *mbr = mbr_read();

  	mbr_partition_ent_t partition;
  	memcpy(&partition, mbr->part_tab1, sizeof(mbr_partition_ent_t));
  	assert(mbr_part_is_fat32(partition.part_type));

  	fat32_fs_t fs = fat32_mk(&partition);

  	pi_dirent_t root = fat32_get_root(&fs);

	char *hello_name = "NEW.PGM";
	fat32_delete(&fs, &root, hello_name);
	fat32_create(&fs, &root, hello_name, 0);
	trace("CREATED\n");
  	int size;
	char *data = buildPGM(&size, 2*RESOLUTION, 2*RESOLUTION, GPU_OUT);
  	pi_file_t hello = (pi_file_t) {
    		.data = data,
    		.n_data = size,
    		.n_alloc = size,
  	};

  	fat32_write(&fs, &root, hello_name, &hello);


	gpu_release(gpu);
}
