.include "../share/vc4inc/vc4.qinc"

# LOAD UNIFORMS INTO REGISTERS  
mov   ra0, unif #A
mov   ra1, unif #B
mov   ra2, unif #C
mov   ra3, unif #N (number of elements to process)

# Initialize loop counter
mov ra10, ra3   # Copy N to loop counter

# Load increment value (16 elements * 4 bytes = 64)
ldi ra4, 64     # Store increment value in ra4

# YOU WILL PROBABLY NEED A LOOP OF SOME SORT
:loop
    # DMA READ A FROM PHYSICAL MEMORY TO VPM
    mov vr_setup, vdr_setup_0(0, 16, 1, dma_h32(0,0))  # Read 1 row of 16 elements
    mov vr_addr, ra0            # Address of A
    mov -, vr_wait              # Wait for read to complete

    # DMA READ B FROM PHYSICAL MEMORY TO VPM
    mov vr_setup, vdr_setup_0(0, 16, 1, dma_h32(1,0))  # Read 1 row of 16 elements
    mov vr_addr, ra1            # Address of B
    mov -, vr_wait              # Wait for read to complete

    # Setup VPM reads - read both A and B into accumulators
    mov vr_setup, vpm_setup(2, 1, h32(0))  # Read 2 rows, increment by 1, horizontal 32-bit
    mov vw_setup, vpm_setup(1, 1, h32(2))  # Write 1 row, increment by 1, horizontal 32-bit

    # Read A and B from VPM into accumulators
    mov r0, vpm                 # Read A into accumulator
    mov -, vr_wait              # Wait for read
    mov r1, vpm                 # Read B into accumulator
    mov -, vr_wait              # Wait for read

    # Add A and B
    add r2, r0, r1              # r2 = A + B

    # Write result back to VPM
    mov vpm, r2                 # Write result
    mov -, vw_wait              # Wait for write

    # DMA WRITE C FROM VPM TO PHYSICAL MEMORY
    mov vw_setup, vdw_setup_0(0, 16, dma_h32(2,0))  # Write 1 row of 16 elements
    mov vw_addr, ra2            # Address of C
    mov -, vw_wait              # Wait for write to complete

    # Update addresses for next iteration using accumulators
    mov r0, ra0                 # Copy A address to accumulator
    mov r1, ra1                 # Copy B address to accumulator
    mov r2, ra2                 # Copy C address to accumulator
    add r0, r0, ra4             # Increment A address
    add r1, r1, ra4             # Increment B address
    add r2, r2, ra4             # Increment C address
    mov ra0, r0                 # Store back to A address
    mov ra1, r1                 # Store back to B address
    mov ra2, r2                 # Store back to C address

    # Check if we should continue the loop
    sub.setf ra10, ra10, 16     # Decrement counter by 16 (we process 16 elements per iteration)
    brr.anynz -, :loop          # Branch if not zero

    # After branch instructions
    nop
    nop
    nop

# End of kernel
:end
thrend
mov interrupt, 1
nop
nop