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
#include <cstdlib> // For system()

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0777)
#endif


using namespace cv;
using namespace std;

// --- MEMORY CONFIGURATION ---
const int POOL_SIZE = 2500; 

// --- GLOBAL VARIABLES ---
queue<Mat*> empty_pool;
queue<Mat*> full_queue;

mutex mtx;
condition_variable cv_writer;
bool is_capturing = true;

// Statistics
int frames_captured = 0;
int frames_written = 0;
int frames_dropped = 0;

// --- DISK WRITER THREAD ---
void writer_thread_func(string filename, int width, int height, double fps) {
    // Still saving as AVI (MJPG) for the fastest disk writing speed
    VideoWriter out(filename, VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, Size(width, height));
    
    if (!out.isOpened()) {
        cerr << "[Writer] Error: Cannot open file for writing!" << endl;
        return;
    }
    
    cout << "[Writer] Ready. Buffer Pool Size: " << POOL_SIZE << " frames." << endl;

    while (true) {
        Mat* frame_ptr = nullptr;

        {
            unique_lock<mutex> lock(mtx);
            // Wait for frames in the queue or until capturing is finished
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

            // Return the empty frame to the pool for reuse
            {
                lock_guard<mutex> lock(mtx);
                empty_pool.push(frame_ptr);
            }
        }
    }
    out.release();
    cout << "[Writer] Finished writing AVI file." << endl;
}

void create_directories() {
    MKDIR("recordings");
    MKDIR("recordings/h264");
}

int main() {
    // 1. Create directories for video files if they don't exist
    create_directories();

    VideoCapture cap(0, CAP_V4L2);
    if (!cap.isOpened()) {
        cerr << "Error: Cannot open camera." << endl;
        return -1;
    }

    // --- CAMERA CONFIGURATION ---
    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(CAP_PROP_FPS, 60);

    // Adjust Exposure (Important to achieve 60fps)
    cap.set(CAP_PROP_AUTO_EXPOSURE, 0.25); 
    cap.set(CAP_PROP_EXPOSURE, 0.01);      

    // --- ALLOCATE RAM ---
    cout << "Allocating memory pool... This might take a moment." << endl;
    vector<Mat> memory_block(POOL_SIZE); 
    for(int i=0; i<POOL_SIZE; i++) {
        memory_block[i] = Mat::zeros(720, 1280, CV_8UC3);
        empty_pool.push(&memory_block[i]);
    }
    
    // Warm up the camera
    for(int i=0; i<20; i++) { Mat t; cap >> t; }

    double frame_width = cap.get(CAP_PROP_FRAME_WIDTH);
    double frame_height = cap.get(CAP_PROP_FRAME_HEIGHT);
    cout << "Camera resolution: " << frame_width << "x" << frame_height << endl;

    // --- GENERATE FILENAME ---
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char buf[100];
    strftime(buf, 100, "%Y-%m-%d_%H-%M-%S", ltm);
    string timestamp_str = string(buf);

    string avi_path = "recordings/" + timestamp_str + ".avi";
    string mp4_path = "recordings/h264/" + timestamp_str + ".mp4";

    // Start the Writer Thread (for AVI)
    thread writer_thread(writer_thread_func, avi_path, (int)frame_width, (int)frame_height, 60.0);

    cout << "--- STARTING RECORDING (60s) ---" << endl;
    auto start_time = chrono::steady_clock::now();
    int duration_seconds = 60;

    Mat temp_frame; 

    // --- CAPTURE LOOP ---
    while (true) {
        cap >> temp_frame; 
        
        if (temp_frame.empty()) {
            cout << "Lost frame from camera!" << endl;
            break;
        }
        
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
            temp_frame.copyTo(*dest_frame); // Copy to RAM buffer
            {
                lock_guard<mutex> lock(mtx);
                full_queue.push(dest_frame);
            }
            cv_writer.notify_one();
        } else {
            frames_dropped++; // Should only happen if RAM is full
        }

        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start_time).count() >= duration_seconds) {
            break;
        }
    }

    // --- END RECORDING ---
    auto end_capture_time = chrono::steady_clock::now();
    double capture_time_ms = chrono::duration_cast<chrono::milliseconds>(end_capture_time - start_time).count();
    double capture_time_s = capture_time_ms / 1000.0;


    cout << "Capture finished. Flushing RAM to Disk... (Do not exit)" << endl;
    
    {
        lock_guard<mutex> lock(mtx);
        is_capturing = false;
    }
    cv_writer.notify_one();
    if (writer_thread.joinable()) writer_thread.join();

    cout << "----------------- REPORT -----------------" << endl;
    cout << "Actual Recording Time: " << capture_time_s << "s" << endl;
    cout << "Frames Captured      : " << frames_captured << " (Avg: " << frames_captured / capture_time_s << " FPS)" << endl;
    cout << "Frames Written       : " << frames_written << endl;
    cout << "Frames Dropped       : " << frames_dropped << endl;
    
    // --- FINAL STEP: CONVERT TO MP4 (FFMPEG) ---
    if (frames_written > 100) {
        cout << "\n[FFmpeg] Converting AVI to MP4..." << endl;
        
        // Standard FFmpeg command + 'ultrafast' preset for speed
        string ffmpeg_cmd = "ffmpeg -y -i " + avi_path + 
                           " -c:v libx264 -pix_fmt yuv420p -vtag avc1 -movflags +faststart " +
                           "-preset ultrafast " + 
                           mp4_path;
                     
        cout << "Executing: " << ffmpeg_cmd << endl;
        int result = system(ffmpeg_cmd.c_str());

        if (result == 0) {
            cout << "[Success] MP4 saved to: " << mp4_path << endl;
            // Uncomment the line below to delete the original AVI file
            // remove(avi_path.c_str());
        } else {
            cerr << "[Error] FFmpeg conversion failed. Please check if FFmpeg is installed and in your PATH." << endl;
        }
    }
    
    return 0;
}