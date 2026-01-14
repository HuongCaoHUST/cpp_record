#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>

using namespace cv;
using namespace std;

const int POOL_SIZE = 2500;

queue<Mat*> empty_pool;
queue<Mat*> full_queue;
mutex mtx;
condition_variable cv_writer;
bool is_capturing = true;

int frames_captured = 0;
int frames_written = 0;
int frames_dropped = 0;

void writer_thread_func(string filename, int width, int height, double fps) {
    VideoWriter out(filename, VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, Size(width, height));
    
    if (!out.isOpened()) {
        cerr << "[Writer] Error: Cannot open file: " << filename << endl;
        return;
    }
    
    cout << "[Writer] Ready. File: " << filename << endl;

    while (true) {
        Mat* frame_ptr = nullptr;
        {
            unique_lock<mutex> lock(mtx);
            cv_writer.wait(lock, []{ return !full_queue.empty() || !is_capturing; });
            if (full_queue.empty() && !is_capturing) break;
            if (!full_queue.empty()) {
                frame_ptr = full_queue.front();
                full_queue.pop();
            }
        }

        if (frame_ptr != nullptr) {
            out.write(*frame_ptr);
            frames_written++;
            {
                lock_guard<mutex> lock(mtx);
                empty_pool.push(frame_ptr);
            }
        }
    }
    out.release();
    cout << "[Writer] Finished writing AVI." << endl;
}

string getCmdOption(char ** begin, char ** end, const std::string & option) {
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end) {
        return std::string(*itr);
    }
    return "";
}

int main(int argc, char** argv) {
    system("mkdir -p /home/comvis/Bee_monitoring/record/h264/");

    VideoCapture cap(0, CAP_V4L2);
    if (!cap.isOpened()) return -1;

    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(CAP_PROP_FPS, 60);
    cap.set(CAP_PROP_AUTO_EXPOSURE, 0.25); 
    cap.set(CAP_PROP_EXPOSURE, 0.01);      

    cout << "Allocating memory pool (~6.2 GB)..." << endl;
    vector<Mat> memory_block(POOL_SIZE); 
    for(int i=0; i<POOL_SIZE; i++) {
        memory_block[i] = Mat::zeros(720, 1280, CV_8UC3);
        empty_pool.push(&memory_block[i]);
    }

    for(int i=0; i<20; i++) { Mat t; cap >> t; }

    double w = cap.get(CAP_PROP_FRAME_WIDTH);
    double h = cap.get(CAP_PROP_FRAME_HEIGHT);
    string input_name = getCmdOption(argv, argv + argc, "--name");
    
    string filename_mp4 = "";
    string filename_base = "";

    if (!input_name.empty()) {
        filename_mp4 = input_name;
        size_t lastindex = input_name.find_last_of("."); 
        if (lastindex != string::npos) { 
            filename_base = input_name.substr(0, lastindex); 
        } else {
            filename_base = input_name;
        }
    } else {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        char buf[100];
        strftime(buf, 100, "%Y-%m-%d_%H-%M-%S", ltm);
        string timestamp_str = string(buf);
        
        filename_base = "pi02_" + timestamp_str;
        filename_mp4 = filename_base + ".mp4";
    }

    string avi_path = "/home/comvis/Bee_monitoring/record/" + filename_base + ".avi";
    string mp4_path = "/home/comvis/Bee_monitoring/record/h264/" + filename_mp4;

    cout << "Target MP4: " << mp4_path << endl;
    cout << "Temp AVI:   " << avi_path << endl;

    thread t_writer(writer_thread_func, avi_path, (int)w, (int)h, 60.0);

    cout << "--- START RECORDING ---" << endl;
    auto start = chrono::steady_clock::now();
    
    Mat temp_frame; 
    while (true) {
        cap >> temp_frame; 
        if (temp_frame.empty()) break;
        
        frames_captured++;

        Mat* dest_frame = nullptr;
        {
            lock_guard<mutex> lock(mtx);
            if (!empty_pool.empty()) {
                dest_frame = empty_pool.front();
                empty_pool.pop();
            }
        }

        if (dest_frame != nullptr) {
            temp_frame.copyTo(*dest_frame);
            {
                lock_guard<mutex> lock(mtx);
                full_queue.push(dest_frame);
            }
            cv_writer.notify_one();
        } else {
            frames_dropped++;
        }

        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() >= 60) {
            break;
        }
    }

    {
        lock_guard<mutex> lock(mtx);
        is_capturing = false;
    }
    cv_writer.notify_one();
    if (t_writer.joinable()) t_writer.join();

    cout << "---------------- REPORT ----------------" << endl;
    cout << "Frames Captured : " << frames_captured << endl;
    cout << "Frames Written  : " << frames_written << endl;

    if (frames_written > 100) {
        cout << "\n[FFmpeg] Converting to MP4..." << endl;
        string cmd = "ffmpeg -y -i " + avi_path + 
                     " -c:v libx264 -pix_fmt yuv420p -vtag avc1 -movflags +faststart " +
                     "-preset ultrafast " + 
                     mp4_path;
        system(cmd.c_str());
    }
    
    return 0;
}