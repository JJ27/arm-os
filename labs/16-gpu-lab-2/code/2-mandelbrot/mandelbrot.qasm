.include "../share/vc4inc/vc4.qinc"

# Read uniforms into registers
mov   ra0, unif # RESOLUTION    
mov   ra1, unif # 1/RESOLUTION
mov   ra2, unif # MAX_ITER
mov   ra3, unif # NUM_QPU
mov   ra4, unif # QPU_NUM
mov   ra5, unif # ADDRESS

mov ra10, ra4           # i = QPU_NUM
mov r1, ra0
shl ra6, r1, 1          # width,height = 2*RESOLUTION

# Store debug values in the first row of output
mov r1, ra4             # Get QPU number
shl r1, r1, 2           # Multiply by 4 (sizeof(uint32_t))
mov r2, vpm_setup(1, 1, h32(0))
add vw_setup, r1, r2    # Add QPU offset to VPM setup

# Calculate y coordinate (matching CPU version)
itof r1, ra10           # (float) i
fmul r1, r1, ra1        # y = i * (1/RESOLUTION)
fsub rb9, r1, 1.0       # y = y - 1.0 (scale to [-1,1])

# Debug: Store y coordinate in VPM
mov vpm, rb9
mov -, vw_wait

:row_loop               # We'll use a nested 2D loop like the CPU example

mov ra11, 0             # j_base = 0

shl r1, ra0, 3          # bytes_per_row = 2*RESOLUTION*sizeof(uint32_t) = 8*RESOLUTION

mov r2, ra10
mul24 ra12, r1, r2      # row_base_address = i * bytes_per_row 

:column_loop
    # Calculate x for each SIMD lane (matching CPU version)
    itof r1, ra11           # Convert j_base to float
    fmul r1, r1, ra1        # x = j_base * (1/RESOLUTION)
    fsub r1, r1, 1.0        # x = x - 1.0 (scale to [-1,1])
    
    # Add offset for each SIMD lane
    itof r2, elem_num       # Convert lane index to float
    fmul r2, r2, ra1        # offset = lane_index * (1/RESOLUTION)
    fadd rb8, r1, r2        # x = x + offset

    # Debug: Store x coordinate in VPM
    mov r1, ra4             # Get QPU number
    shl r1, r1, 2           # Multiply by 4 (sizeof(uint32_t))
    mov r2, vpm_setup(1, 1, h32(1))
    add vw_setup, r1, r2    # Add QPU offset to VPM setup
    mov vpm, rb8
    mov -, vw_wait

    # Initialize u, v, u2, v2 to 0 (all floats)
    itof r1, 0
    mov rb0, r1     # u
    mov rb1, r1     # v
    mov rb2, r1     # u2
    mov rb3, r1     # v2
      
    mov ra7, 1      # Start with k=1 like CPU version
    mov rb7, 1      # Start with 1 (in set), will set to 0 if diverges

:inner_loop
    # Update u2 and v2 - use accumulators to avoid register file conflicts
    mov r0, rb0     # Copy u to accumulator
    mov r1, rb1     # Copy v to accumulator
    fmul r2, r0, r0 # u2 = u * u
    fmul r3, r1, r1 # v2 = v * v
    mov rb2, r2     # Store u2
    mov rb3, r3     # Store v2

    # Update u and v - use accumulators for all operations
    fsub r0, r2, r3      # u = u2 - v2
    mov r1, rb8          # Copy x to accumulator
    fadd r0, r0, r1      # u = u + x
    mov rb0, r0          # Store u

    mov r0, rb0          # Copy u to accumulator
    mov r1, rb1          # Copy v to accumulator
    fmul r0, r0, r1      # v = u * v
    fadd r0, r0, r0      # v = v * 2
    mov r1, rb9          # Copy y to accumulator
    fadd r0, r0, r1      # v = v + y
    mov rb1, r0          # Store v

    # Check for divergence (u^2 + v^2 > 4)
    mov r0, rb2          # Copy u2 to accumulator
    mov r1, rb3          # Copy v2 to accumulator
    fadd r0, r0, r1      # r0 = u2 + v2
    fsub.setf r0, r0, 4.0  # Set flags if r0 > 4.0

    # Debug: Store divergence check value in VPM
    mov r1, ra4             # Get QPU number
    shl r1, r1, 2           # Multiply by 4 (sizeof(uint32_t))
    mov r2, vpm_setup(1, 1, h32(2))
    add vw_setup, r1, r2    # Add QPU offset to VPM setup
    mov vpm, r0
    mov -, vw_wait

    # Set output to 0 if diverged (using proper condition code)
    mov.ifn rb7, 0

    # Exit if all lanes have diverged
    brr.alln -, :exit
    nop
    nop
    nop

    # UPDATE OUR ITERATION COUNTER (like CPU version)
    add ra7, ra7, 1      # Increment k
    mov r1, ra2          # Get MAX_ITER
    sub.setf r1, ra7, r1 # Check if k >= MAX_ITER
    brr.anyc -, :exit    # Exit if k >= MAX_ITER
    nop
    nop
    nop

    brr -, :inner_loop
    nop
    nop
    nop

    :exit

    # Debug: Store iteration count in VPM
    mov r1, ra4             # Get QPU number
    shl r1, r1, 2           # Multiply by 4 (sizeof(uint32_t))
    mov r2, vpm_setup(1, 1, h32(3))
    add vw_setup, r1, r2    # Add QPU offset to VPM setup
    mov vpm, ra7            # Store k (iteration count)
    mov -, vw_wait

    # Calculate the correct VPM address for this row
    mov r1, ra10          # Get current row number
    shl r1, r1, 4         # Multiply by 16 (bytes per row)
    mov r2, vpm_setup(1, 1, h32(4))  # Use offset 4 for output values
    add vw_setup, r1, r2  # Add row offset to VPM setup

    # Write the 16-wide vector to VPM (ensure we write 1 or 0)
    mov vpm, rb7          # Write the row to VPM (rb7 is already 1 or 0)
    mov -, vw_wait

    # Calculate the correct physical memory address
    mov r1, ra11          # j_base
    shl r1, r1, 2         # Multiply by 4 (sizeof(uint32_t))
    add r1, ra12, r1      # Add row_base_address
    add r1, ra5, r1       # Add base address

    # Setup DMA transfer (write 16 32-bit values)
    mov r2, vdw_setup_0(1, 16, dma_h32(0,0))
    mov vw_setup, r2
    mov vw_addr, r1
    mov -, vw_wait

    # Update our j_base value and check the column loop condition
    add ra11, ra11, 16

    #  Is j_base < width? 
    mov r1, ra6
    sub.setf r1, ra11, r1
    brr.anyc -, :column_loop

    nop
    nop
    nop

    # Update our i value and check the row loop condition
    mov r1, ra3
    add ra10, ra10, r1         #  i += NUM_QPU

    # Is i < height (2*RESOLUTION)? 
    mov r1, ra6
    sub.setf r1, ra10, r1 
    brr.anyc -, :row_loop
    nop
    nop
    nop

# End of kernel
:end
thrend
mov interrupt, 1
nop

