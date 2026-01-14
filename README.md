# High-FPS Video Recorder

This is a C++ application that records high-framerate video from a webcam using OpenCV. It's designed to minimize dropped frames by using a multi-threaded producer-consumer approach, where frames are temporarily stored in a RAM buffer before being written to disk.

The application first saves the recording as a high-speed MJPEG AVI file and then automatically converts it to a standard H.264 MP4 file using FFmpeg.

## Features

- Records video from a webcam at a specified resolution and framerate (default: 1280x720 @ 60fps).
- Uses a large in-memory frame buffer to prevent frame drops during capture.
- A dedicated writer thread handles writing video frames to disk to avoid blocking the capture thread.
- Automatically converts the initial AVI file to a compressed MP4 using FFmpeg.
- Cross-platform support for Windows and Linux.

## Prerequisites

Before you build and run this project, you need to have the following software installed on your system:

- **CMake**: For building the project.
- **A C++ Compiler**: Like GCC (on Linux) or MSVC (on Windows).
- **OpenCV**: The library used for camera interaction. Make sure it's installed and its path is accessible by CMake.
- **FFmpeg**: Required for converting the video to MP4. Make sure the `ffmpeg` executable is in your system's PATH.

## How to Build

You can build the project using CMake with the following commands from the root directory of the project:

1.  **Create a build directory:**
    ```bash
    mkdir build
    ```

2.  **Navigate into the build directory:**
    ```bash
    cd build
    ```

3.  **Run CMake to configure the project:**
    ```bash
    cmake ..
    ```
    *If CMake has trouble finding OpenCV, you may need to specify its location. For example:*
    ```bash
    cmake .. -DOpenCV_DIR=/path/to/your/opencv/build
    ```

4.  **Compile the project:**
    - On **Linux**:
      ```bash
      make
      ```
    - On **Windows** (with MSVC):
      ```bash
      cmake --build . --config Release
      ```

## How to Run

After a successful build, the executable `bee_record` (or `bee_record.exe` on Windows) will be located in the `build` directory.

Run it from the project's root directory to ensure it can create the `recordings` folder correctly.

```bash
./build/bee_record
```

Recorded videos will be saved in the `recordings` and `recordings/h264` directories.
