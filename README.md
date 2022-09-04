# resize
This is my image image resize algorithm. It outputs an image that has equal light output of the input. This is a high quality method for reducing the size of an image.

To compile, simply run ./compile.sh
To set up tooling with clangd, type:
```
bear -- ./compile.sh
```
to create compile_commands.json

This is a very slow algorithm. It operates in O(oldwidth\*newwidth\*oldheight\*newheight/(gcd(oldwidth,newwidth)\*gcd(oldheight,newheight)))

