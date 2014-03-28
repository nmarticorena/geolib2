#include "geolib/sensors/DepthCamera.h"
#include "geolib/Shape.h"

namespace geo {

DepthCamera::DepthCamera() {
}

DepthCamera::~DepthCamera() {

}

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
//
//                                        RAYTRACING
//
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

void DepthCamera::render(const Shape& shape, const Pose3D& pose, cv::Mat& image) {

    //Transform tf_map_to_sensor(pose);
#ifdef GEOLIB_USE_TF
    tf::Transform pose_in = pose.inverse();
#else
    Transform pose_in = pose.inverse();
#endif

    for(int my = 0; my < image.rows; ++my) {
        for(int mx = 0; mx < image.cols; ++mx) {
            Vector3 dir((double)mx / image.cols - 0.5, (double)my / image.rows - 0.5, 1);
            dir.normalize();

            Ray r(Vector3(0, 0, 0), dir);

            Vector3 dir_transformed = pose_in.getBasis() * r.direction_;
            Ray r_transformed(pose_in.getOrigin(), dir_transformed);

            //std::cout << r_transformed.origin_ << std::endl;

            double distance = 0;
            if (shape.intersect(r_transformed, 0, 10, distance)) {
                if (image.at<float>(my, mx) == 0 || distance < image.at<float>(my, mx)) {
                    image.at<float>(my, mx) = distance;
                }
            }
        }
    }
}

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
//
//                                        RASTERIZATION
//
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

RasterizeResult DepthCamera::rasterize(const Shape& shape, const Pose3D& cam_pose, const Pose3D& obj_pose, cv::Mat& image,
                                       PointerMap& pointer_map, void* pointer, TriangleMap& triangle_map) const {
#ifdef GEOLIB_USE_TF
    tf::Transform t = cam_pose.inverse() * obj_pose;
    return rasterize(shape, geo::Pose3D(t.getOrigin(), t.getRotation()), image, pointer_map, pointer);
#else
    return rasterize(shape, cam_pose.inverse() * obj_pose, image, pointer_map, pointer);
#endif
}

RasterizeResult DepthCamera::rasterize(const Shape& shape, const Pose3D& pose, cv::Mat& image,
                                       PointerMap& pointer_map, void* pointer, TriangleMap& triangle_map) const {

    // reserve pointer map
    if (pointer) {
        if ((int)pointer_map.size() != image.cols || (int)pointer_map[0].size() != image.rows) {
            pointer_map.resize(image.cols, std::vector<void*>(image.rows, NULL));
        }
    }

    // reserve triangle map
    if ((int)triangle_map.size() != image.cols || (int)triangle_map[0].size() != image.rows) {
        triangle_map.resize(image.cols, std::vector<int>(image.rows, -1));
    }

    RasterizeResult res;
    res.min_x = image.cols;
    res.min_y = image.rows;
    res.max_x = 0;
    res.max_y = 0;

#ifdef GEOLIB_USE_TF
    tf::Transform pose_in = pose;
#else
    Transform pose_in = pose;
#endif

    if (shape.getMaxRadius() < pose_in.getOrigin().z()) {
        return res;
    }

    //pose_in.setOrigin(-pose.getOrigin());
    //tf::Transform pose_in = Pose3D(0, 0, -5, 2.3, 0.3, 0.3);//pose.inverse();

    double near_clip_z = -0.1;

    const std::vector<TriangleI>& triangles = shape.getMesh().getTriangleIs();
    const std::vector<Vector3>& points = shape.getMesh().getPoints();

    // transform points
    std::vector<Vector3> points_t(points.size());
    std::vector<cv::Point2d> points_proj(points.size());

    for(unsigned int i = 0; i < points.size(); ++i) {
        points_t[i] = pose_in * points[i];
        points_proj[i] = project3Dto2D(points_t[i], image.cols, image.rows);
    }

    int i_triangle = 0;
    for(std::vector<TriangleI>::const_iterator it_tri = triangles.begin(); it_tri != triangles.end(); ++it_tri) {
        const Vector3& p1_3d = points_t[it_tri->i1_];
        const Vector3& p2_3d = points_t[it_tri->i2_];
        const Vector3& p3_3d = points_t[it_tri->i3_];

        int n_verts_in = 0;
        bool v1_in = false;
        bool v2_in = false;
        bool v3_in = false;
        const Vector3* vIn[3];

        if (p1_3d.z() < near_clip_z) {
            ++n_verts_in;
            v1_in = true;
        }

        if (p2_3d.z() < near_clip_z) {
            ++n_verts_in;
            v2_in = true;
        }

        if (p3_3d.z() < near_clip_z) {
            ++n_verts_in;
            v3_in = true;
        }

        if (n_verts_in == 1) {
            if (v1_in) { vIn[0] = &(p1_3d); vIn[1] = &(p2_3d); vIn[2] = &(p3_3d); }
            if (v2_in) { vIn[0] = &(p2_3d); vIn[1] = &(p3_3d); vIn[2] = &(p1_3d); }
            if (v3_in) { vIn[0] = &(p3_3d); vIn[1] = &(p1_3d); vIn[2] = &(p2_3d); }

            //Parametric line stuff
            // p = v0 + v01*t
            Vector3 v01 = *vIn[1] - *vIn[0];

            float t1 = ((near_clip_z - (*vIn[0]).z()) / v01.z() );

            Vector3 new2(vIn[0]->x() + v01.x() * t1, vIn[0]->y() + v01.y() * t1, near_clip_z);

            // Second vert point
            Vector3 v02 = *vIn[2] - *vIn[0];

            float t2 = ((near_clip_z - (*vIn[0]).z()) / v02.z());

            Vector3 new3(vIn[0]->x() + v02.x() * t2, vIn[0]->y() + v02.y() * t2, near_clip_z);

            drawTriangle(*vIn[0], new2, new3, image, pointer_map, pointer, triangle_map, i_triangle, res);
        } else if (n_verts_in == 2) {
            if (!v1_in) { vIn[0]=&(p2_3d); vIn[1]=&(p3_3d); vIn[2]=&(p1_3d); }
            if (!v2_in) { vIn[0]=&(p3_3d); vIn[1]=&(p1_3d); vIn[2]=&(p2_3d); }
            if (!v3_in) { vIn[0]=&(p1_3d); vIn[1]=&(p2_3d); vIn[2]=&(p3_3d); }

            //Parametric line stuff
            // p = v0 + v01*t
            Vector3 v01 = *vIn[2] - *vIn[0];

            float t1 = ((near_clip_z - (*vIn[0]).z())/v01.z() );

            Vector3 new2((*vIn[0]).x() + v01.x() * t1,(*vIn[0]).y() + v01.y() * t1, near_clip_z);

            // Second point
            Vector3 v02 = *vIn[2] - *vIn[1];

            float t2 = ((near_clip_z - (*vIn[1]).z())/v02.z());

            Vector3 new3((*vIn[1]).x() + v02.x() * t2, (*vIn[1]).y() + v02.y() * t2, near_clip_z);

            drawTriangle(*vIn[0], *vIn[1], new2, image, pointer_map, pointer, triangle_map, i_triangle, res);

            drawTriangle(new2, *vIn[1], new3, image, pointer_map, pointer, triangle_map, i_triangle, res);

        } else if (n_verts_in == 3) {
            const cv::Point2d& p1_2d = points_proj[it_tri->i1_];
            const cv::Point2d& p2_2d = points_proj[it_tri->i2_];
            const cv::Point2d& p3_2d = points_proj[it_tri->i3_];

            drawTriangle(p1_2d, -p1_3d.z(),
                         p2_2d, -p2_3d.z(),
                         p3_2d, -p3_3d.z(),
                         image, pointer_map, pointer, triangle_map, i_triangle, res);
        }

        ++i_triangle;
    }

    /*
    for(int y = 0; y < image.rows; ++y) {
        for(int x = 0; x < image.cols; ++x) {
            if (image.at<float>(y, x) > 9) {
                image.at<float>(y, x) = 0.0 / 0.0;
            }
        }
    }
    */

    return res;
}

void DepthCamera::drawTriangle(const Vector3& p1_3d, const Vector3& p2_3d, const Vector3& p3_3d, cv::Mat& image,
                               PointerMap& pointer_map, void* pointer, TriangleMap& triangle_map, int i_triangle,
                               RasterizeResult& res) const {
    cv::Point2d p1_2d = project3Dto2D(p1_3d, image.cols, image.rows);
    cv::Point2d p2_2d = project3Dto2D(p2_3d, image.cols, image.rows);
    cv::Point2d p3_2d = project3Dto2D(p3_3d, image.cols, image.rows);

    drawTriangle(p1_2d, -p1_3d.z(),
                 p2_2d, -p2_3d.z(),
                 p3_2d, -p3_3d.z(), image, pointer_map, pointer, triangle_map, i_triangle, res);
}


void DepthCamera::drawTriangle(const cv::Point2d& p1, float d1,
                               const cv::Point2d& p2, float d2,
                               const cv::Point2d& p3, float d3, cv::Mat& image,
                               PointerMap& pointer_map, void* pointer,
                               TriangleMap& triangle_map, int i_triangle,
                               RasterizeResult& res) const {

    if ((p2.x - p1.x) * (p3.y - p1.y) - (p3.x - p1.x) * (p2.y - p1.y) < 0) {        
        int min_y = std::min<int>(p1.y, std::min<int>(p2.y, p3.y));
        int max_y = std::max<int>(p1.y, std::max<int>(p2.y, p3.y));
        int min_x = std::min<int>(p1.x, std::min<int>(p2.x, p3.x));
        int max_x = std::max<int>(p1.x, std::max<int>(p2.x, p3.x));


        if (min_x < image.cols && max_x > 0 && min_y < image.rows && max_y > 0) {
            res.min_x = std::max(0, std::min<int>(res.min_x, min_x));
            res.min_y = std::max(0, std::min<int>(res.min_y, min_y));
            res.max_x = std::min(image.cols - 1, std::max<int>(res.max_x, max_x));
            res.max_y = std::min(image.rows - 1, std::max<int>(res.max_y, max_y));

            float d1_inv = 1 / d1;
            float d2_inv = 1 / d2;
            float d3_inv = 1 / d3;

            cv::Point2d p_min, p_mid, p_max;
            float d_min, d_mid, d_max;
            if (p1.y < p2.y) {
                if (p2.y < p3.y) {
                    p_min = p1; p_mid = p2; p_max = p3;
                    d_min = d1_inv; d_mid = d2_inv; d_max = d3_inv;
                } else if (p3.y < p1.y) {
                    p_min = p3; p_mid = p1; p_max = p2;
                    d_min = d3_inv; d_mid = d1_inv; d_max = d2_inv;
                } else {
                    p_min = p1; p_mid = p3; p_max = p2;
                    d_min = d1_inv; d_mid = d3_inv; d_max = d2_inv;
                }
            } else {
                if (p1.y < p3.y) {
                    p_min = p2; p_mid = p1; p_max = p3;
                    d_min = d2_inv; d_mid = d1_inv; d_max = d3_inv;
                } else if (p3.y < p2.y) {
                    p_min = p3; p_mid = p2; p_max = p1;
                    d_min = d3_inv; d_mid = d2_inv; d_max = d1_inv;
                } else {
                    p_min = p2; p_mid = p3; p_max = p1;
                    d_min = d2_inv; d_mid = d3_inv; d_max = d1_inv;
                }
            }

            int y_min_mid = (int)p_mid.y - (int)p_min.y;
            int y_mid_max = (int)p_max.y - (int)p_mid.y;
            int y_min_max = (int)p_max.y - (int)p_min.y;

            if (y_min_max == 0) {
                return;
            }

            int p_prime_x = (y_mid_max * p_min.x + y_min_mid * p_max.x) / y_min_max;
            float d_prime = (d_min * y_mid_max + d_max * y_min_mid) / y_min_max;

            int ax, bx;
            float ad, bd;
            if (p_prime_x < p_mid.x) {
                ax = p_prime_x; bx = p_mid.x;
                ad = d_prime; bd = d_mid;
            } else {
                ax = p_mid.x; bx = p_prime_x;
                ad = d_mid; bd = d_prime;
            }

            blaa(image, p_min.y, p_mid.y,
                 p_min.x, (ax - p_min.x) / y_min_mid, p_min.x, (bx - p_min.x) / y_min_mid,
                 d_min, (ad - d_min) / y_min_mid, d_min, (bd - d_min) / y_min_mid);

            blaa(image, p_mid.y, p_max.y,
                 ax, (p_max.x - ax) / y_mid_max, bx, (p_max.x - bx) / y_mid_max,
                 ad, (d_max - ad) / y_mid_max, bd, (d_max - bd) / y_mid_max);
        }
    }
}

// -------------------------------------------------------------------------------

void DepthCamera::blaa(cv::Mat& image, int y_start, int y_end,
                        float x_start, float x_start_delta, float x_end, float x_end_delta,
                        float d_start, float d_start_delta, float d_end, float d_end_delta) const {

    if (y_start < 0) {
        d_start += d_start_delta * -y_start;
        d_end += d_end_delta * -y_start;
        x_start += x_start_delta * -y_start;
        x_end += x_end_delta * -y_start;
        y_start = 0;
    }

    y_end = std::min(image.rows - 1, y_end);

    for(int y = y_start; y < y_end; ++y) {
        float d = d_start;
        float d_delta = (d_end - d_start) / (x_end - x_start);

        int x_start2;
        if (x_start < 0) {
            d += d_delta * -x_start;
            x_start2 = 0;
        } else {
            x_start2 = x_start;
        }

        int x_end2 = std::min(image.cols - 1, (int)x_end);

        for(int x = x_start2; x <= x_end2; ++x) {
            image.at<float>(y, x) = 1.0f / d;
            d += d_delta;
        }

        d_start+= d_start_delta;
        d_end += d_end_delta;
        x_start += x_start_delta;
        x_end += x_end_delta;
    }
}































void DepthCamera::drawSpansBetweenEdges(const Edge& e1, const Edge& e2, cv::Mat& image,
                                        PointerMap& pointer_map, void* pointer,
                                        TriangleMap& triangle_map, int i_triangle) const {
    // calculate difference between the y coordinates
    // of the first edge and return if 0
    float e1ydiff = (float)(e1.Y2 - e1.Y1);
    if(e1ydiff == 0.0f)
        return;

    // calculate difference between the y coordinates
    // of the second edge and return if 0
    float e2ydiff = (float)(e2.Y2 - e2.Y1);
    if(e2ydiff == 0.0f)
        return;

    // calculate differences between the x coordinates
    // and colors of the points of the edges
    float e1xdiff = (float)(e1.X2 - e1.X1);
    float e2xdiff = (float)(e2.X2 - e2.X1);
    float e1colordiff = (e1.Color2 - e1.Color1);
    float e2colordiff = (e2.Color2 - e2.Color1);

    // calculate factors to use for interpolation
    // with the edges and the step values to increase
    // them by after drawing each span
    float factor1 = (float)(e2.Y1 - e1.Y1) / e1ydiff;
    float factorStep1 = 1.0f / e1ydiff;
    float factor2 = 0.0f;
    float factorStep2 = 1.0f / e2ydiff;


    if (e2.Y1 < 0) {
        factor1 += -e2.Y1 * factorStep1;
        factor2 += -e2.Y1 * factorStep2;
    }

    // loop through the lines between the edges and draw spans
    for(int y = std::max(0, e2.Y1); y < std::min(e2.Y2, image.rows); y++) {
        if (y >= 0 && y < image.rows) {
            //cout << y << endl;
            // create and draw span
            Span span(e1.Color1 + (e1colordiff * factor1),
                      e1.X1 + (int)(e1xdiff * factor1),
                      e2.Color1 + (e2colordiff * factor2),
                      e2.X1 + (int)(e2xdiff * factor2));
            drawSpan(span, y, image, pointer_map, pointer, triangle_map, i_triangle);
        }

        // increase factors
        factor1 += factorStep1;
        factor2 += factorStep2;
    }
}

void DepthCamera::drawSpan(const Span &span, int y, cv::Mat& image,
                           PointerMap& pointer_map, void* pointer,
                           TriangleMap& triangle_map, int i_triangle) const {

//    std::cout << "    Span: " << span.X1 << " - " << span.X2 << std::endl;

    int xdiff = span.X2 - span.X1;
    if(xdiff == 0)
        return;

    float colordiff = span.Color2 - span.Color1;

    float factor = 0.0f;
    float factorStep = 1.0f / (float)xdiff;

    if (span.X1 < 0) {
        factor += -span.X1 * factorStep;
    }


    // draw each pixel in the span
    for(int x = std::max(0, span.X1); x < std::min(span.X2, image.cols); x++) {
        if (x >= 0 && x < image.cols) {
            float depth = 1 / (span.Color1 + (colordiff * factor));
            float old_depth = image.at<float>(y, x);
            if (old_depth == 0 || old_depth > depth) {
                image.at<float>(y, x) = depth;
                if (pointer) {
                    pointer_map[x][y] = pointer;
                }

                if (!triangle_map.empty()) {
                    triangle_map[x][y] = i_triangle;
                }
            }

//            std::cout << "        Pixel: " << x << " , " << y << ": " << depth << std::endl;
        }
        factor += factorStep;
    }
}

}
