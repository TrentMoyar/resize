#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <lcms2.h>
#include <jpeglib.h>

typedef struct {
  double x,y,z;
} xyz;

typedef struct {
  xyz  *xyz;
  size_t width,height;
  cmsHPROFILE icc_profile;
} image;

struct my_error_mgr {
  struct jpeg_error_mgr pub; /* "public" fields */

  jmp_buf setjmp_buffer; /* for return to caller */
};

typedef struct my_error_mgr *my_error_ptr;

cmsHPROFILE jankcopy(cmsHPROFILE profile) {
  cmsUInt32Number icclen;
  cmsSaveProfileToMem(profile,NULL,&icclen);
  JOCTET *iccmem = malloc(icclen);
  cmsSaveProfileToMem(profile,iccmem,&icclen);
  cmsHPROFILE ret = cmsOpenProfileFromMem(iccmem, icclen);
  free(iccmem);
  return ret;
}

void freeimage(image img) {
  free(img.xyz);
  cmsCloseProfile(img.icc_profile);
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
  output.xyz = calloc(newwidth*newheight,sizeof(xyz));
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
  output.icc_profile = jankcopy(input.icc_profile);

  for(uint32_t y = 0; y < fullheight; y++) {
      for(uint32_t x = 0; x < fullwidth; x++) {
          output.xyz[(y/newheightdiv)*newwidth + (x/newwidthdiv)].x += input.xyz[(y/oldheightdiv)*oldwidth + (x/oldwidthdiv)].x;
          output.xyz[(y/newheightdiv)*newwidth + (x/newwidthdiv)].y += input.xyz[(y/oldheightdiv)*oldwidth + (x/oldwidthdiv)].y;
          output.xyz[(y/newheightdiv)*newwidth + (x/newwidthdiv)].z += input.xyz[(y/oldheightdiv)*oldwidth + (x/oldwidthdiv)].z;
      }
  }
  double factor = (double)(newwidthdiv*newheightdiv);
  for(uint32_t i = 0; i < newwidth*newheight; i++) {
      output.xyz[i].x /= factor;
      output.xyz[i].y /= factor;
      output.xyz[i].z /= factor;
  }
  return output;
}

void write_JPEG_file(char *filename, image img)
{
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cmsHPROFILE inprofile = cmsCreateXYZProfile();
  cmsHPROFILE outprofile = img.icc_profile;
  cmsHTRANSFORM transform = cmsCreateTransform(inprofile,
                                                TYPE_XYZ_DBL,
                                                outprofile,
                                                TYPE_RGB_8,
                                                INTENT_PERCEPTUAL,
                                                cmsFLAGS_NOOPTIMIZE);
  cmsCloseProfile(inprofile);
  FILE *outfile;                /* target file */
  JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */
  int row_stride;               /* physical row width in image buffer */
  cinfo.err = jpeg_std_error(&jerr);
  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress(&cinfo);
  if ((outfile = fopen(filename, "wb")) == NULL) {
    fprintf(stderr, "can't open %s\n", filename);
    exit(1);
  }
  jpeg_stdio_dest(&cinfo, outfile);
  cinfo.image_width = img.width;      /* image width and height, in pixels */
  cinfo.image_height = img.height;
  cinfo.input_components = 3;           /* # of color components per pixel */
  cinfo.in_color_space = JCS_RGB;       /* colorspace of input image */
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 75, TRUE /* limit to baseline-JPEG values */);
  jpeg_start_compress(&cinfo, TRUE);
  cmsUInt32Number icclen;
  cmsSaveProfileToMem(img.icc_profile,NULL,&icclen);
  JOCTET *iccmem = malloc(icclen);
  cmsSaveProfileToMem(img.icc_profile,iccmem,&icclen);
  jpeg_write_icc_profile(&cinfo,iccmem,icclen);
  free(iccmem);
  row_stride = img.width * 3; /* JSAMPLEs per row in image_buffer */
  JSAMPLE *image_buffer = malloc(img.width*3);
  int row = 0;
  while (cinfo.next_scanline < cinfo.image_height) {
    cmsDoTransform(transform, img.xyz + row*img.width, image_buffer, img.width);
    jpeg_write_scanlines(&cinfo, &image_buffer, 1);
    row++;
  }
  free(image_buffer);
  cmsDeleteTransform(transform);
  jpeg_finish_compress(&cinfo);
  fclose(outfile);
  jpeg_destroy_compress(&cinfo);
}

void my_error_exit(j_common_ptr cinfo) {
  my_error_ptr myerr = (my_error_ptr)cinfo->err;
  (*cinfo->err->output_message)(cinfo);
  longjmp(myerr->setjmp_buffer, 1);
}

int read_JPEG_file(char *filename, image *img) {
  struct jpeg_decompress_struct cinfostruct;
  struct jpeg_decompress_struct *cinfo = &cinfostruct;
  struct my_error_mgr jerr;
  /* More stuff */
  FILE *infile;      /* source file */
  JSAMPARRAY buffer; /* Output row buffer */
  int row_stride;    /* physical row width in output buffer */

  if ((infile = fopen(filename, "rb")) == NULL) {
    fprintf(stderr, "can't open %s\n", filename);
    return 0;
  }

  cinfo->err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;
  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(cinfo);
    fclose(infile);
    return 0;
  }
  jpeg_create_decompress(cinfo);
  jpeg_stdio_src(cinfo, infile);
  jpeg_save_markers(cinfo, JPEG_APP0 + 2, 0xFFFF);
  (void)jpeg_read_header(cinfo, TRUE);
  J_COLOR_SPACE out_space = cinfo->out_color_space;
  JOCTET *icc_data_ptr;
  unsigned int icc_data_len;
  boolean found = jpeg_read_icc_profile(cinfo, &icc_data_ptr, &icc_data_len);
  cmsHPROFILE inprofile;
  if(found) {
    inprofile = cmsOpenProfileFromMem(icc_data_ptr, icc_data_len);
    free(icc_data_ptr);
  } else {
    inprofile = cmsCreate_sRGBProfile();
  }
  cmsColorSpaceSignature color_space = cmsGetColorSpace(inprofile);
  if(color_space != cmsSigRgbData || out_space != JCS_RGB) {
    printf("Incorrect colorspace of image, can't resize.\n");
    cmsCloseProfile(inprofile);
    jpeg_destroy_decompress(cinfo);
    fclose(infile);
    return 0;
  }
  cmsHPROFILE outprofile = cmsCreateXYZProfile();
  cmsHTRANSFORM transform = cmsCreateTransform(inprofile,
                                                TYPE_RGB_8,
                                                outprofile,
                                                TYPE_XYZ_DBL,
                                                INTENT_ABSOLUTE_COLORIMETRIC,0);
  cmsCloseProfile(outprofile);
  img->icc_profile = inprofile;
  jpeg_start_decompress(cinfo);
  img->width = cinfo->output_width;
  img->height = cinfo->output_height;
  img->xyz = calloc(sizeof(xyz)*img->width*img->height,1);
  row_stride = cinfo->output_width * cinfo->output_components;
  buffer = (*cinfo->mem->alloc_sarray)((j_common_ptr)cinfo, JPOOL_IMAGE,
                                       row_stride, 1);
  int row = 0;
  while (cinfo->output_scanline < cinfo->output_height) {
    unsigned int readed = jpeg_read_scanlines(cinfo, buffer, 1);
    cmsDoTransform(transform, buffer[0], img->xyz + row*img->width, img->width);
    row++;
  }
  cmsDeleteTransform(transform);
  jpeg_finish_decompress(cinfo);
  jpeg_destroy_decompress(cinfo);
  fclose(infile);
  return 1;
}

int main(int argc, char **argv) {
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
    image img;
    int read = read_JPEG_file(argv[1],&img);
    if(read) {
      image resize = resample(img,outwidth,outheight);
      freeimage(img);
      write_JPEG_file(argv[4], resize);
      freeimage(resize);
    } else {
      printf("could not read image\n");
    }
}