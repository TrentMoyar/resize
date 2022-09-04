#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wand/MagickWand.h>

typedef struct {
  double  *reds, *blues, *greens;
  size_t width,height;
} image;

void freeimage(image input) {
  free(input.reds);
  free(input.greens);
  free(input.blues);
}

uint32_t gcd(uint32_t one, uint32_t two) {
    while(two != 0) {
        uint32_t r = one % two;
        one = two;
        two = r;
    }
    return one;
}

image resample(image input, int newwidth, int newheight) {
  image output;
  output.reds = calloc(newwidth*newheight,sizeof(double));
  output.blues = calloc(newwidth*newheight,sizeof(double));
  output.greens = calloc(newwidth*newheight,sizeof(double));

  uint16_t oldwidth = input.width;
  uint16_t oldheight = input.height;
  uint32_t fullwidth  = oldwidth*newwidth/gcd(oldwidth,newwidth);
  uint32_t fullheight = oldheight*newheight/gcd(oldheight,newheight);
  uint16_t oldwidthdiv  = newwidth/gcd(oldwidth,newwidth);
  uint16_t oldheightdiv = newheight/gcd(oldheight,newheight);
  uint16_t newwidthdiv  = oldwidth/gcd(oldwidth,newwidth);
  uint16_t newheightdiv = oldheight/gcd(oldheight,newheight);

  output.height = newheight;
  output.width = newwidth;

  for(uint32_t y = 0; y < fullheight; y++) {
      for(uint32_t x = 0; x < fullwidth; x++) {
          output.reds[(y/newheightdiv)*newwidth + (x/newwidthdiv)]   += input.reds[(y/oldheightdiv)*oldwidth + (x/oldwidthdiv)];
          output.greens[(y/newheightdiv)*newwidth + (x/newwidthdiv)] += input.greens[(y/oldheightdiv)*oldwidth + (x/oldwidthdiv)];
          output.blues[(y/newheightdiv)*newwidth + (x/newwidthdiv)]  += input.blues[(y/oldheightdiv)*oldwidth + (x/oldwidthdiv)];
      }
  }
  double factor = (double)(newwidthdiv*newheightdiv);
  for(uint32_t i = 0; i < newwidth*newheight; i++) {
      output.reds[i]   /= factor;
      output.greens[i] /= factor;
      output.blues[i]  /= factor;
  }
  return output;
}

image readimage(char *filename) {
  image input;
  MagickWand *input_wand = NewMagickWand();
  MagickBooleanType status = MagickReadImage(input_wand, filename);
  if (status == MagickFalse) {
    printf("Could not read input image.");
    errno = 1;
    return input;
  }
  if(errno != 0) {
    printf("Potential error reading image, errno = %d\n", errno);
  }
  errno = 0;
  input.width = MagickGetImageWidth(input_wand);
  input.height = MagickGetImageHeight(input_wand);
  input.reds = malloc(sizeof(double) * input.width * input.height);
  input.blues = malloc(sizeof(double) * input.width * input.height);
  input.greens = malloc(sizeof(double) * input.width * input.height);
  int count = 0;
  unsigned long number_wands;
  PixelIterator *iterator = NewPixelIterator(input_wand);
  PixelWand **pixels;
  for (int y = 0; y < input.height; y++) {
    pixels = PixelGetNextIteratorRow(iterator, &number_wands);
    for (int x = 0; x < number_wands; x++) {
      input.reds[count] = PixelGetRed(pixels[x]);
      input.greens[count] = PixelGetGreen(pixels[x]);
      input.blues[count] = PixelGetBlue(pixels[x]);
      count++;
    }
    PixelSyncIterator(iterator);
  }
  iterator = DestroyPixelIterator(iterator);
  input_wand = DestroyMagickWand(input_wand);
  return input;
}

void saveimage(image input, char *filename) {
  PixelWand *p_wand = NewPixelWand();
	PixelSetColor(p_wand,"black");
	MagickWand *m_wand = NewMagickWand();
	// Create a 100x100 image with a default of white
	MagickNewImage(m_wand,input.width,input.height,p_wand);
	// Get a new pixel iterator
	PixelIterator *iterator = NewPixelIterator(m_wand);
  PixelWand **pixels;
  int count = 0;
	for(int y = 0; y < input.height; y++) {
		// Get the next row of the image as an array of PixelWands
    unsigned long wands;
		pixels=PixelGetNextIteratorRow(iterator,&wands);
		// Set the row of wands to a simple gray scale gradient
		for(int x = 0; x < wands; x++) {
			PixelSetRed(pixels[x],input.reds[count]);
      PixelSetBlue(pixels[x],input.blues[count]);
      PixelSetGreen(pixels[x],input.greens[count]);
      PixelSetAlpha(pixels[x], 0.01);
      count++;
		}
		// Sync writes the pixels back to the m_wand
		PixelSyncIterator(iterator);
	}
	MagickWriteImage(m_wand,filename);
	// Clean up
	iterator=DestroyPixelIterator(iterator);
	DestroyMagickWand(m_wand);
}

int main(int argc, char **argv) {
  MagickWandGenesis();

  MagickBooleanType status;

  if (argc != 5) {
    printf("Needs 4 arguments: input width height output\n");
    printf("Example: wand input.jpg 1000 900 output.png\n");
    return 0;
  }
  if (strcmp(argv[1], argv[4]) == 0) {
    printf("Input and output images cannot have the same name.\n");
    return 0;
  }
  int outwidth = atoi(argv[2]);
  int outheight = atoi(argv[3]);
  if (outwidth <= 0 || outheight <= 0) {
    printf("Width and height are not properly formatted.\n");
    return 0;
  }
  errno = 0;
  image input = readimage(argv[1]);
  if(errno != 0) {
    printf("Could not read image.\n");
    return 0;
  }
  image output = resample(input, outwidth, outheight);
  saveimage(output, argv[4]);

  freeimage(input);
  MagickWandTerminus();
  return (0);
}