# filmgrain-simplified

![banner](samples/readme_images/food_grain.png)

Reconstructing a photo with binary film grain, a simplified take on [A Stochastic Film Grain Model for Resolution-Independent Rendering](https://hal.science/hal-01520260/file/Film_grain_synthesis_computer_graphics_forum.pdf).

All GPU-based, C + SDL3-GPU + HLSL.

This repo accompanies this article: [Simple Physically-Based Film Grain Simulation: An Experiment](http://kanzwataru.xsrv.jp/articles/2025-07-04-grain-experiment-en.html)

## Running Builds

You can get binaries for Windows and Linux from the Releases section.

This is a command-line tool.
If you run it with no arguments, a list of parameters and their explanation is output.
The simplest command to run is `filmgrain-simplified path/to/source.png path/to/output.tga`.

You can use either 8-bit or 16-bit images as the input.
Output is 8-bit. Format is auto-detected by file extension.

## Compiling from Source

1. Ensure you have `SDL3`.
2. If you modified shaders, use `compile_and_embed_shaders.py`. This requires the `dxc` compiler. *(You do not need to run this if you didn't modify shaders because the embedded header file is submitted in the repo)*
3. If you modified the noise texture, use `embed_resources.py`. *(The generated file is included in the repo)*
4. Use `build.sh` to compile.

Basically for just compiling, without modifying shaders or resources, you just need to run `build.sh`.
