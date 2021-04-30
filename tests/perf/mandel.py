
jobs = 1

mid_x = -0.5
mid_y = 0.0
render_width  = 2.5
render_height = 2.5
iterations    = 256

width  = 50
height = 25

output = [0] * (width * height)

def mandelbrot_int(cx, cy):
    int_precision = 28

    zx = 0
    zy = 0

    cx = int(cx * (1 << int_precision))
    cy = int(cy * (1 << int_precision))

    depth = None

    for d in range(iterations):
        sq_zx = (zx * zx) >> int_precision
        sq_zy = (zy * zy) >> int_precision

        if sq_zx + sq_zy > (4 << int_precision):
            depth = d
            break

        new_zx = sq_zx - sq_zy + cx
        zy = ((2 * zx * zy) >> int_precision) + cy
        zx = new_zx

    return depth

def mandelbrot_float(cx, cy):
    zx = 0
    zy = 0

    depth = 0

    for d in range(iterations) :
        sq_zx = zx * zx
        sq_zy = zy * zy

        if sq_zx + sq_zy > 4 :
            depth = d
            break

        new_zx = sq_zx - sq_zy + cx
        zy = (2 * zx * zy) + cy
        zx = new_zx

    return depth

def generate_mandelbrot_range(job, mandel_func):
    for iy in range(job, height, jobs):
        for ix in range(0, width):
            i = iy * width + ix

            cx = (ix - (width  / 2)) * (render_width  / width)  + mid_x
            cy = (iy - (height / 2)) * (render_height / height) + mid_y

            depth = mandel_func(cx, cy)

            output[i] = "#" if depth == 0 else " " #chr(max(32, min(126, depth)))

def main(use_float = False):
    mandel_func = mandelbrot_float if use_float else mandelbrot_int

    generate_mandelbrot_range(0, mandel_func)

    for y in range(height):
        start = y * width
        print("".join(output[start : start + width]))

if __name__ == "__main__":
    main(True)
