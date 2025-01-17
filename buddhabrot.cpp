#include <chrono>
#include <complex>
#include <future>
#include <iostream>
#include <png.h>
#include <random>
#include <thread>
#include <vector>

using idx = std::ptrdiff_t;
using pt = std::complex<double>;  // a point in real life
using px = std::pair<idx, idx>;   // pixel in the image

class buddhabrot {
   private:
    static constexpr double escape_radius2 = 8.0;
    const idx image_size;
    const idx iterations;
    const idx max_samples;
    const idx stride;
    const idx stride_offset;
    std::vector<std::vector<double>> image;
    std::vector<std::vector<pt>> buf;
    std::vector<idx> buflen;
    std::mt19937 engine;

    /**
     * struct to represent a bounding box
     */
    struct bounds {
        double ulo, uhi, vlo, vhi;
    };

    /**
     * sample a random point within bounding box
     */
    pt random_pt(const bounds& bb) {
        std::uniform_real_distribution<double> uniform_dist_real(bb.ulo,
                                                                 bb.uhi);
        std::uniform_real_distribution<double> uniform_dist_imag(bb.vlo,
                                                                 bb.vhi);
        return pt(uniform_dist_real(engine), uniform_dist_imag(engine));
    }

    /**
     * convert point to pixel
     */
    px to_px(const pt z) {
        return std::make_pair(
            static_cast<idx>(image_size * (z.real() + 2) / 4),
            static_cast<idx>(image_size * (z.imag() + 2) / 4));
    }

    /**
     * convert pixel to point
     */
    pt to_pt(const px x) {
        return pt(x.first * 4.0 / image_size - 2.0,
                  x.second * 4.0 / image_size - 2.0);
    }

    /**
     * check if a pixel is within bounds of the image
     */
    bool in_bounds(px y) {
        return y.first >= 0 && y.second >= 0 && y.first < image_size &&
               y.second < image_size;
    }

    /**
     * Render a region within bounding box
     *
     * This is an adaptive sampling algorithm that uses importance sampling.
     * The importance of the box is assumed to be 10 * the max path length
     * originating from a point in the region.
     *
     * As we continue to sample, we keep updating the importance of the region
     * as needed.
     *
     * For example, for points in the Mandelbrot set, after 5 samples, it will
     * immediately terminate. However, interesting points tend to be on the
     * edges of the set. So, cells that contain points both in and out of the
     * Mandelbrot set will be considered to have maximum importance.
     */
    void render_region(const bounds& bb) {
        idx samples = 5;
        idx max_path = -1;
        for (idx trial = 0; trial < samples; trial++) {
            auto c = random_pt(bb);
            pt z(0, 0);
            idx escaped_time = 0;
            for (idx i = 0; i < iterations; i++) {
                z = z * z + c;
                buf[trial][i] = z;
                if (z.imag() * z.imag() + z.real() * z.real() >
                    escape_radius2) {
                    escaped_time = i;
                    break;
                }
            }

            // the longer the path, the higher the importance.
            if (escaped_time > max_path) {
                max_path = escaped_time;
                samples = std::max(
                    samples,
                    std::min(max_samples, 5 + 2 * max_path * max_path));
            }

            // if we encounter the edge of the mandelbrot set, we treat this as
            // the maximum importance.
            if ((escaped_time == 0 && max_path > 0) ||
                (escaped_time != 0 && max_path == -1)) {
                samples = max_samples;
            }

            buflen[trial] = escaped_time;
        }

        const double weight = 1.0 / samples;
        for (idx trial = 0; trial < samples; trial++) {
            for (idx i = 0; i < buflen[trial]; i++) {
                px y = to_px(buf[trial][i]);
                if (in_bounds(y)) image[y.first][y.second] += weight;
            }
        }
    }

   public:
    buddhabrot(const idx image_size_, const idx iterations_,
               const idx max_samples_, const idx seed, const idx stride_ = 1,
               const idx stride_offset_ = 0)
        : image_size(image_size_),
          iterations(iterations_),
          max_samples(max_samples_),
          stride(stride_),
          stride_offset(stride_offset_),
          image(image_size, std::vector<double>(image_size, 0)),
          buf(max_samples, std::vector<pt>(iterations)),
          buflen(max_samples),
          engine(seed) {}

    void render() {
        for (idx u = stride_offset; u < image_size; u += stride) {
            for (idx v = 0; v < image_size; v++) {
                pt a = to_pt(std::make_pair(u, v));
                pt b = to_pt(std::make_pair(u + 1, v + 1));
                render_region(bounds{a.real(), b.real(), a.imag(), b.imag()});
            }
        }
    }

    double operator()(idx u, idx v) const { return image[u][v]; }
};

/**
 * combine the buddhabrots from all the different threads
 * and write them to a png file
 */
 


void write(const std::string& filename,
           const std::vector<std::unique_ptr<buddhabrot>>& brots,
           const idx image_size) {
    double max_val = 0;
    double min_val = std::numeric_limits<double>::infinity();

    for (idx u = 0; u < image_size; u++) {
        for (idx v = 0; v < image_size; v++) {
            double x = 0;
            for (auto& b : brots) {
                x += (*b)(u, v);
            }
            if (x < min_val) {
                min_val = x;
            }
            if (x > max_val) {
                max_val = x;
            }
        }
    }
    
    std::cout << "max_val: " << max_val << std::endl;
    std::cout << "min_val: " << min_val << std::endl;

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        std::cerr << "Error: Unable to create file " << filename << std::endl;
        return;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::cerr << "Error: Failed to create PNG write struct" << std::endl;
        fclose(fp);
        return;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        std::cerr << "Error: Failed to create PNG info struct" << std::endl;
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return;
    }

    png_init_io(png, fp);

    png_set_IHDR(png, info, image_size, image_size, 16, PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    std::vector<png_byte> image_data(image_size * image_size * 2, 0);

    for (idx u = 0; u < image_size; u++) {
        for (idx v = 0; v < image_size; v++) {
            double x = 0;
            for (auto& b : brots) {
                x += (*b)(u, v);
                x += (*b)(u, image_size - 1 - v);
            }
            double normalized_x = (x - min_val) / (max_val - min_val);
            png_uint_16 pixel_value = static_cast<png_uint_16>(normalized_x * ((1 << 16) - 1));
            image_data[(u * image_size + v) * 2] = static_cast<png_byte>(pixel_value >> 8); // Higher byte
            image_data[(u * image_size + v) * 2 + 1] = static_cast<png_byte>(pixel_value); // Lower byte
        }
    }

    for (idx y = 0; y < image_size; y++) {
        png_write_row(png, &image_data[y * image_size * 2]);
    }

    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    fclose(fp);
}



int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "USAGE: buddhabrot image_size iterations num_threads "
                     "max_samples_per_pixel\n"
                  << "example: buddhabrot 1024 1000 12 64" << std::endl;
        return 1;
    }

    const idx image_size = std::atoi(argv[1]);
    const idx iterations = std::atoi(argv[2]);
    const idx n_threads = std::atoi(argv[3]);
    const idx max_samples = std::atoi(argv[4]);

    std::vector<std::unique_ptr<buddhabrot>> brots;
    for (idx i = 0; i < n_threads; i++) {
        std::random_device rd;
        auto seed =
            std::chrono::steady_clock::now().time_since_epoch().count() + i +
            rd();
        brots.emplace_back(std::make_unique<buddhabrot>(
            image_size, iterations, max_samples, seed, n_threads, i));
    }

    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (idx i = 0; i < n_threads; i++) {
        threads.emplace_back([=, &brots]() { brots[i]->render(); });
    }
    for (idx i = 0; i < n_threads; i++) {
        threads[i].join();
    }
    std::stringstream filename_ss;
    filename_ss << "buddhabrot_" << image_size << "_" << iterations << "_"
                << max_samples << ".png";
    write(filename_ss.str(), brots, image_size);
    return 0;
}

// void write(const std::string& filename,
//            const std::vector<std::unique_ptr<buddhabrot>>& brots,
//            const idx image_size) {
//     double max_val = 0;
//     double min_val = std::numeric_limits<double>::infinity();
//     for (idx u = 0; u < image_size; u++) {
//         for (idx v = 0; v < image_size; v++) {
//             double x = 0;
//             for (auto& b : brots) {
//                 x += (*b)(u, v);
//             }
//             if (x < min_val) {
//                 min_val = x;
//             }
//             if (x > max_val) {
//                 max_val = x;
//             }
//         }
//     }

//     png::image<png::gray_pixel_16> pimage(image_size, image_size);
//     for (idx u = 0; u < image_size; u++) {
//         for (idx v = 0; v < image_size; v++) {
//             double x = 0;
//             for (auto& b : brots) {
//                 x += (*b)(u, v);
//                 x += (*b)(u, image_size - 1 - v);
//             }
//             pimage[u][v] = png::gray_pixel_16(
//                 ((1 << 16) - 1) *
//                 std::sqrt((x * 0.5 - min_val) / (max_val - min_val)));
//         }
//     }
//     pimage.write(filename);
// }