#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <ctime>

using namespace cv;
using namespace std;

int main(int argc, char** argv) {
    VideoCapture cap(0, CAP_V4L2);
    
    if (!cap.isOpened()) {
        cerr << "ERROR: Cannot open camera" << endl;
        return -1;
    }

    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(CAP_PROP_FPS, 60);

    cout << "Warming up camera..." << endl;
    for(int i=0; i<30; i++) {
        Mat temp;
        cap >> temp;
    }

    double width = cap.get(CAP_PROP_FRAME_WIDTH);
    double height = cap.get(CAP_PROP_FRAME_HEIGHT);
    cout << "Camera Config: " << width << "x" << height << endl;

    // --- TẠO FILE OUTPUT ---
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char filename[100];
    strftime(filename, 100, "record_%Y-%m-%d_%H-%M-%S.mp4", ltm);
    
    // Mẹo: Nếu hệ thống yếu, giảm FPS ghi xuống 30 để video mượt hơn
    // Hoặc đo FPS thực tế trong vòng lặp warm-up để điền vào đây.
    double save_fps = 60.0; 

    VideoWriter out(filename, VideoWriter::fourcc('m', 'p', '4', 'v'), save_fps, Size((int)width, (int)height));

    if (!out.isOpened()) {
        cerr << "ERROR: Could not open video writer" << endl;
        return -1;
    }

    cout << "Recording to " << filename << " for 60 seconds..." << endl;

    // --- VÒNG LẶP CHÍNH ---
    auto start_time = chrono::steady_clock::now();
    int frame_count = 0;
    int duration_sec = 60; 

    while (true) {
        Mat frame;
        cap >> frame; 

        if (frame.empty()) {
            cerr << "ERROR: Blank frame" << endl;
            break;
        }

        out.write(frame);
        frame_count++;

        // Kiểm tra thời gian
        auto current_time = chrono::steady_clock::now();
        auto elapsed_seconds = chrono::duration_cast<chrono::seconds>(current_time - start_time).count();

        if (elapsed_seconds >= duration_sec) {
            break;
        }
    }

    auto end_time = chrono::steady_clock::now();
    double total_duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count() / 1000.0;
    
    cout << "Finished." << endl;
    cout << "Frames captured: " << frame_count << endl;
    cout << "Real Duration: " << total_duration << "s" << endl;
    cout << "Average FPS: " << frame_count / total_duration << endl;

    cap.release();
    out.release();
    return 0;
}