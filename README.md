![SampleFinder](https://i.imgur.com/OdlPuSU.png)

**SampleFinder** is a song recognition tool tailored specifically for finding samples in music. It's meant to provide the user with more control and recognition data than apps like Shazam or Google's song recognition feature.

Pros:
- Lets you view multiple recognition results
- Uses a local audio library instead of an online database
- Gives you control over all the recognition algorithm's parameters
- Spectrogram display w/ peaks/standout signal plots

Cons:
- More cumbersome to use than other software. It's an ad-hoc tool for a very specific purpose, and its UI/UX design reflects that.
- Slower to use/less efficient than other software. While the song recognition itself isn't slow, processing and indexing the user-imported audio library is. That's the trade-off with using local data.

## Running the software

Grab a build off the releases page on GitHub or from my website.

1. Extract the .zip somewhere
2. Run **SampleFinder.exe**
3. Import your audio library, and you're all set!

## Building prerequisites

- C++ compiler with support for C++17
- Boost (1.81.0+)
- SDL2 (2.26.2+)
- GLEW (2.1.0+)
- SDL_mixer (2.6.2+)
- OpenCV (4.7.0+)
- libsndfile (1.2.0+)

### On Linux

If you're using a Debian-based distro, run this to install the packages you'll probably need:

`sudo apt-get install libboost-dev libsdl2-dev libsdl2-mixer-dev libglew-dev libsndfile-dev libopencv-dev`

If you're using something else just install the equivalent packages for all the dependencies listed above.

Anyway, I didn't write a `Makefile` or `CMakeLists.txt` for this, so write one yourself and make a pull request if that's how you want to build the software :^)

## Credits

- KP (me; UI, rendering, library management and song recognition)
- Will Drevo (author of the [Dejavu](https://github.com/worldveil/dejavu) project SampleFinder is loosely based on)
- Suliman Alsowelim (author of this [C++ port](https://github.com/salsowelim/dejavu_cpp_port) of Dejavu's fingerprinting algorithm)

## Licensing

This software is made available under the [MIT License](https://choosealicense.com/licenses/mit/). See LICENSE.md for more information.
