/*

 MIT License

 Copyright © 2021 Samuel Venable

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

*/

#include <string>

namespace ngs::imgui {

  std::string get_open_filename(std::string filter, std::string fname);
  std::string get_open_filename_ext(std::string filter, std::string fname, std::string dir, std::string title);
  std::string get_open_filenames(std::string filter, std::string fname);
  std::string get_open_filenames_ext(std::string filter, std::string fname, std::string dir, std::string title);
  std::string get_save_filename(std::string filter, std::string fname);
  std::string get_save_filename_ext(std::string filter, std::string fname, std::string dir, std::string title);
  std::string get_directory(std::string dname);
  std::string get_directory_alt(std::string capt, std::string root);

} // namespace ngs::imgui

#if defined(IFD_SHARED_LIBRARY)
#ifdef _WIN32
#define EXPORTED_FUNCTION extern "C" __declspec(dllexport)
#else
#define EXPORTED_FUNCTION extern "C" __attribute__((visibility("default")))
#endif
EXPORTED_FUNCTION void ifd_load_fonts();
EXPORTED_FUNCTION char *get_open_filename(char *filter, char *fname);
EXPORTED_FUNCTION char *get_open_filename_ext(char *filter, char *fname, char *dir, char *title);
EXPORTED_FUNCTION char *get_open_filenames(char *filter, char *fname);
EXPORTED_FUNCTION char *get_open_filenames_ext(char *filter, char *fname, char *dir, char *title);
EXPORTED_FUNCTION char *get_save_filename(char *filter, char *fname);
EXPORTED_FUNCTION char *get_save_filename_ext(char *filter, char *fname, char *dir, char *title);
EXPORTED_FUNCTION char *get_directory(char *dname);
EXPORTED_FUNCTION char *get_directory_alt(char *capt, char *root);
#endif
