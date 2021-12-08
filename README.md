# Hybrid Stochastic Reflections Sample 1.0

Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Hybrid Stochastic Reflections (HSR)

![Screenshot](screenshot.png)

Hybrid Stochastic Reflections (HSR) Sample is an open source, high-quality solution for producing physically based hybrid reflections combining screen space traversal with hardware ray tracing.

It uses a per pixel feedback pipeline to predict the majority ray class per 8x8 tiles in screen space. This allows to avoid hybridization cost.

You can find the binaries for HSR in the release section on GitHub. 

## Features
* Hybrid reflections
    * Using FidelityFX SSSR reflections to replace raytracing intersection tests
    * Reshading re-projected pixels for accurate material evaluation
    * Supports lower resolution reflections using FSR 1.0 upscaling
    * Using per-pixel feedback to reduce hybridization cost
* Raytraced reflections

## Requirements
* A graphics card with Direct3D® 12 and DXR support.
* Windows® 10 (64-bit recommended).
* Visual Studio® 2019 with Visual C++® and the Windows® 10 SDK installed.
## Instructions

To build the HSR Sample, please follow the following instructions:

1) Install the following tools:
    - [CMake 3.16](https://cmake.org/download/)
    - [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
    - [Windows 10 SDK 10.0.18362.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)

2) Clone repository
3) Generate the solutions:
    ```
    > cd <installation path>\build
    > GenerateSolutions.bat
    ```

3) Open the solutions in the DX12 directory (depending on your preference), compile and run.

