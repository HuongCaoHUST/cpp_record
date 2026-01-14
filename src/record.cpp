#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace cv;
using namespace std;

queue<Mat> frame_queue;
mutex mtx;
condition_variable cv_writer;

bool capture_finished = false; // Báo hiệu đã quay xong

void writer_thread_func(string filename, int width, int height, double fps) {
    VideoWriter out(filename, VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, Size(width, height));
    
    if (!out.isOpened()) {
        cerr << "[Writer] Error: Could not open video file." << endl;
        return;
    }
    
    cout << "[Writer] Thread started. Waiting for data..." << endl;
    int frames_saved = 0;

    while (true) {
        Mat frame;
        {
            unique_lock<mutex> lock(mtx);
            cv_writer.wait(lock, []{ return !frame_queue.empty() || capture_finished; });
            if (capture_finished && frame_queue.empty()) {
                break;
            }

            if (!frame_queue.empty()) {
                frame = frame_queue.front();
                frame_queue.pop();
            }
        } 
        
        if (!frame.empty()) {
            out.write(frame);
            frames_saved++;
            if (frames_saved % 100 == 0) {
                cout << "[Writer] Saving... (" << frames_saved << " frames done)" << endl;
            }
        }
    }
    
    out.release();
    cout << "[Writer] Done. Total frames saved to disk: " << frames_saved << endl;
}

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
    for(int i=0; i<10; i++) { Mat t; cap >> t; }

    double width = cap.get(CAP_PROP_FRAME_WIDTH);
    double height = cap.get(CAP_PROP_FRAME_HEIGHT);
    cout << "Camera Config: " << width << "x" << height << endl;
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char filename_buffer[100];
    strftime(filename_buffer, 100, "%Y-%m-%d_%H-%M-%S.avi", ltm);
    string output_path = "/home/comvis/Bee_monitoring/record/" + string(filename_buffer);
    thread t_writer(writer_thread_func, output_path, (int)width, (int)height, 60.0);

    cout << "Main Thread: Start capturing to RAM..." << endl;
    
    auto start_time = chrono::steady_clock::now();
    int frame_count = 0;
    int duration_seconds = 60;
    while (true) {
        Mat frame;
        cap >> frame;

        if (frame.empty()) {
            cout << "Lost frame form camera!" << endl;
            break;
        }
        {
            lock_guard<mutex> lock(mtx);
            frame_queue.push(frame.clone()); 
        }
        cv_writer.notify_one(); 
        
        frame_count++;
        auto current_time = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(current_time - start_time).count() >= duration_seconds) {
            break;
        }
    }
    auto end_capture_time = chrono::steady_clock::now();
    double capture_duration = chrono::duration_cast<chrono::milliseconds>(end_capture_time - start_time).count() / 1000.0;
    
    cout << "---------------- CAPTURE FINISHED ----------------" << endl;
    cout << "Captured " << frame_count << " frames in " << capture_duration << "s." << endl;
    cout << "Avg Capture FPS: " << frame_count / capture_duration << endl;
    
    {
        lock_guard<mutex> lock(mtx);
        capture_finished = true; 
    }
    cv_writer.notify_one();
    size_t remaining = 0;
    {
        lock_guard<mutex> lock(mtx);
        remaining = frame_queue.size();
    }
    cout << "Please wait! Flushing " << remaining << " frames from RAM to Disk..." << endl;
    cout << "DO NOT TURN OFF..." << endl;

    if (t_writer.joinable()) {
        t_writer.join();
    }

    cout << "---------------- ALL DONE ----------------" << endl;
    cout << "Video saved: " << output_path << endl;

    return 0;
}