#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <fstream>
#include <algorithm>

#include "ImFileDialog.h"
#include "ImFileDialogMacros.h"
#include "filedialogs.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <lmcons.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Shell32.lib")
#endif
#else
#if (defined(__MACH__) && defined(__APPLE__))
#include <AppKit/AppKit.h>
#endif
#include <unistd.h>
#endif
#include <sys/stat.h>

#define ICON_SIZE ImGui::GetFont()->FontSize + 3
#define GUI_ELEMENT_SIZE std::max(GImGui->FontSize + 10.f, 24.f)
#define DEFAULT_ICON_SIZE 32
#define PI 3.141592f

namespace ifd {
  static const char* GetDefaultFolderIcon();
  static const char* GetDefaultFileIcon();

  /* UI CONTROLS */
  bool FolderNode(const char* label, ImTextureID icon, bool& clicked) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    clicked = false;

    ImU32 id = window->GetID(label);
    int opened = window->StateStorage.GetInt(id, 0);
    ImVec2 pos = window->DC.CursorPos;
    const bool is_mouse_x_over_arrow = (g.IO.MousePos.x >= pos.x && g.IO.MousePos.x < pos.x + g.FontSize);
    if (ImGui::InvisibleButton(label, ImVec2(-FLT_MIN, g.FontSize + g.Style.FramePadding.y * 2))) {
      if (is_mouse_x_over_arrow) {
        int* p_opened = window->StateStorage.GetIntRef(id, 0);
        opened = *p_opened = !*p_opened;
      } else {
        clicked = true;
      }
    }
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    bool doubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (doubleClick && hovered) {
      int* p_opened = window->StateStorage.GetIntRef(id, 0);
      opened = *p_opened = !*p_opened;
      clicked = false;
    }
    if (hovered || active)
      window->DrawList->AddRectFilled(g.LastItemData.Rect.Min, g.LastItemData.Rect.Max, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[active ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered]));
    
    // Icon, text
    float icon_posX = pos.x + g.FontSize + g.Style.FramePadding.y;
    float text_posX = icon_posX + g.Style.FramePadding.y + ICON_SIZE;
    ImGui::RenderArrow(window->DrawList, ImVec2(pos.x, pos.y+g.Style.FramePadding.y), ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[((hovered && is_mouse_x_over_arrow) || opened) ? ImGuiCol_Text : ImGuiCol_TextDisabled]), opened ? ImGuiDir_Down : ImGuiDir_Right);
    window->DrawList->AddImage(icon, ImVec2(icon_posX, pos.y), ImVec2(icon_posX + ICON_SIZE, pos.y + ICON_SIZE));
    ImGui::RenderText(ImVec2(text_posX, pos.y + g.Style.FramePadding.y), label);
    if (opened)
      ImGui::TreePush(label);
    return opened != 0;
  }

  bool FileNode(const char* label, ImTextureID icon) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    //ImU32 id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    bool ret = ImGui::InvisibleButton(label, ImVec2(-FLT_MIN, g.FontSize + g.Style.FramePadding.y * 2));

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    if (hovered || active)
      window->DrawList->AddRectFilled(g.LastItemData.Rect.Min, g.LastItemData.Rect.Max, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[active ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered]));

    // Icon, text
    window->DrawList->AddImage(icon, ImVec2(pos.x, pos.y), ImVec2(pos.x + ICON_SIZE, pos.y + ICON_SIZE));
    ImGui::RenderText(ImVec2(pos.x + g.Style.FramePadding.y + ICON_SIZE, pos.y + g.Style.FramePadding.y), label);
    
    return ret;
  }

  bool PathBox(const char* label, ghc::filesystem::path& path, char* pathBuffer, ImVec2 size_arg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
      return false;

    bool ret = false;
    const ImGuiID id = window->GetID(label);
    int* state = window->StateStorage.GetIntRef(id, 0);
    
    ImGui::SameLine();

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 uiPos = ImGui::GetCursorPos();
    ImVec2 size = ImGui::CalcItemSize(size_arg, 200, GUI_ELEMENT_SIZE);
    const ImRect bb(pos, pos + size);
    
    // buttons
    if (!(*state & 0b001)) {
      ImGui::PushClipRect(bb.Min, bb.Max, false);

      // background
      bool hovered = g.IO.MousePos.x >= bb.Min.x && g.IO.MousePos.x <= bb.Max.x &&
        g.IO.MousePos.y >= bb.Min.y && g.IO.MousePos.y <= bb.Max.y;
      bool clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
      bool anyOtherHC = false; // are any other items hovered or clicked?
      window->DrawList->AddRectFilled(pos, pos + size, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[(*state & 0b10) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg]));

      // fetch the buttons (so that we can throw some away if needed)
      std::vector<std::string> btnList;
      float totalWidth = 0.0f;
      for (auto comp : path) {
        std::string section = comp.string();
        if (section.size() == 1 && (section[0] == '\\' || section[0] == '/'))
          continue;

        totalWidth += ImGui::CalcTextSize(section.c_str()).x + style.FramePadding.x * 2.0f + GUI_ELEMENT_SIZE;
        btnList.push_back(section);
      }
      totalWidth -= GUI_ELEMENT_SIZE;

      // UI buttons
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      bool isFirstElement = true;
      for (size_t i = 0; i < btnList.size(); i++) {
        if (totalWidth > size.x - 30 && i != btnList.size() - 1) { // trim some buttons if there's not enough space
          float elSize = ImGui::CalcTextSize(btnList[i].c_str()).x + style.FramePadding.x * 2.0f + GUI_ELEMENT_SIZE;
          totalWidth -= elSize;
          continue;
        }

        ImGui::PushID(static_cast<int>(i));
        if (!isFirstElement) {
          ImGui::ArrowButtonEx("##dir_dropdown", ImGuiDir_Right, ImVec2(GUI_ELEMENT_SIZE, GUI_ELEMENT_SIZE));
          anyOtherHC |= ImGui::IsItemHovered() | ImGui::IsItemClicked();
          ImGui::SameLine();
        }
        if (ImGui::Button(btnList[i].c_str(), ImVec2(0, GUI_ELEMENT_SIZE))) {
          #ifdef _WIN32
          std::string newPath = "";
          #else
          std::string newPath = "/";
          #endif
          for (size_t j = 0; j <= i; j++) {
            newPath += btnList[j];
            #ifdef _WIN32
            if (j != i)
              newPath += "\\";
            #else
            if (j != i)
              newPath += "/";
            #endif
          }
          path = ghc::filesystem::path(newPath);
          ret = true;
        }
        anyOtherHC |= ImGui::IsItemHovered() | ImGui::IsItemClicked();
        ImGui::SameLine();
        ImGui::PopID();

        isFirstElement = false;
      }
      ImGui::PopStyleVar(2);
      // click state
      if (!anyOtherHC && clicked) {
        strcpy(pathBuffer, path.string().c_str());
        *state |= 0b001;
        *state &= 0b011; // remove SetKeyboardFocus flag
      }
      else
        *state &= 0b110;

      // hover state
      if (!anyOtherHC && hovered && !clicked) 
        *state |= 0b010;
      else 
        *state &= 0b101;

      ImGui::PopClipRect();

      // allocate space
      ImGui::SetCursorPos(uiPos);
      ImGui::ItemSize(size);
    }
    // input box
    else {
      bool skipActiveCheck = false;
      if (!(*state & 0b100)) {
        skipActiveCheck = true;
        ImGui::SetKeyboardFocusHere();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          *state |= 0b100;
      }
      ghc::filesystem::path compare_with_root = pathBuffer;
      if (ImGui::InputTextEx("##pathbox_input", "", pathBuffer, 1024, size_arg, ImGuiInputTextFlags_EnterReturnsTrue) && 
        #if defined(_WIN32)
        (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(compare_with_root.wstring().c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) &&
        #endif
        compare_with_root.string() != compare_with_root.root_name().string() + "\\" &&
        compare_with_root.string() != compare_with_root.root_name().string() + "/" &&
        compare_with_root.string() != compare_with_root.root_name().string()) {
        std::error_code ec;
        std::string tempStr(pathBuffer);
        if (ghc::filesystem::exists(tempStr, ec))
          path = ghc::filesystem::path(tempStr); 
        ret = true;
      }
      if (!skipActiveCheck && !ImGui::IsItemActive())
        *state &= 0b010;
    }

    return ret;
  }

  bool FavoriteButton(const char* label, bool isFavorite) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    ImVec2 pos = window->DC.CursorPos;
    bool ret = ImGui::InvisibleButton(label, ImVec2(GUI_ELEMENT_SIZE, GUI_ELEMENT_SIZE));
    
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    float size = g.LastItemData.Rect.Max.x - g.LastItemData.Rect.Min.x;

    int numPoints = 5;
    float innerRadius = size / 4;
    float outerRadius = size / 2;
    float angle = PI / numPoints;
    ImVec2 center = ImVec2(pos.x + size / 2, pos.y + size / 2);

    // fill
    if (isFavorite || hovered || active) {
      ImU32 fillColor = 0xff00ffff;// ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
      if (hovered || active)
        fillColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[active ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered]);

      // since there is no PathFillConcave, fill first the inner part, then the triangles
      // inner
      window->DrawList->PathClear();
      for (int i = 1; i < numPoints * 2; i += 2)
        window->DrawList->PathLineTo(ImVec2(center.x + innerRadius * sin(i * angle), center.y - innerRadius * cos(i * angle)));
      window->DrawList->PathFillConvex(fillColor);

      // triangles
      for (int i = 0; i < numPoints; i++) {
        window->DrawList->PathClear();

        int pIndex = i * 2;
        window->DrawList->PathLineTo(ImVec2(center.x + outerRadius * sin(pIndex * angle), center.y - outerRadius * cos(pIndex * angle)));
        window->DrawList->PathLineTo(ImVec2(center.x + innerRadius * sin((pIndex + 1) * angle), center.y - innerRadius * cos((pIndex + 1) * angle)));
        window->DrawList->PathLineTo(ImVec2(center.x + innerRadius * sin((pIndex - 1) * angle), center.y - innerRadius * cos((pIndex - 1) * angle)));

        window->DrawList->PathFillConvex(fillColor);
      }
    }

    // outline
    window->DrawList->PathClear();
    for (int i = 0; i < numPoints * 2; i++) {
      float radius = i & 1 ? innerRadius : outerRadius;
      window->DrawList->PathLineTo(ImVec2(center.x + radius * sin(i * angle), center.y - radius * cos(i * angle)));
    }
    window->DrawList->PathStroke(ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]), true, 2.0f);

    return ret;
  }

  bool FileIcon(const char* label, bool isSelected, ImTextureID icon, ImVec2 size, bool hasPreview, int previewWidth, int previewHeight) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float windowSpace = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImVec2 pos = window->DC.CursorPos;
    bool ret = false;

    if (ImGui::InvisibleButton(label, size))
      ret = true;

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    bool doubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (doubleClick && hovered)
      ret = true;


    float iconSize = size.y - g.FontSize * 2;
    float iconPosX = pos.x + (size.x - iconSize) / 2.0f;
    ImVec2 textSize = ImGui::CalcTextSize(label, 0, true, size.x);

    
    if (hovered || active || isSelected)
      window->DrawList->AddRectFilled(g.LastItemData.Rect.Min, g.LastItemData.Rect.Max, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[active ? ImGuiCol_HeaderActive : (isSelected ? ImGuiCol_Header : ImGuiCol_HeaderHovered)]));

    if (hasPreview) {
      ImVec2 availSize = ImVec2(size.x, iconSize);

      float scale = std::min<float>(availSize.x / previewWidth, availSize.y / previewHeight);
      availSize.x = previewWidth * scale;
      availSize.y = previewHeight * scale;

      float previewPosX = pos.x + (size.x - availSize.x) / 2.0f;
      float previewPosY = pos.y + (iconSize - availSize.y) / 2.0f;

      window->DrawList->AddImage(icon, ImVec2(previewPosX, previewPosY), ImVec2(previewPosX + availSize.x, previewPosY + availSize.y));
    } else
      window->DrawList->AddImage(icon, ImVec2(iconPosX, pos.y), ImVec2(iconPosX + iconSize, pos.y + iconSize));
    
    window->DrawList->AddText(g.Font, g.FontSize, ImVec2(pos.x + (size.x-textSize.x) / 2.0f, pos.y + iconSize), ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]), label, 0, size.x);


    float lastButtomPos = ImGui::GetItemRectMax().x;
    float thisButtonPos = lastButtomPos + style.ItemSpacing.x + size.x; // Expected position if next button was on same line
    if (thisButtonPos < windowSpace)
      ImGui::SameLine();

    return ret;
  }

  FileDialog::FileData::FileData(const ghc::filesystem::path& path) {
    std::error_code ec;
    Path = path;
    IsDirectory = ghc::filesystem::is_directory(path, ec);
    Size = (size_t)ghc::filesystem::file_size(path, ec);

    struct stat attr;
    stat(path.string().c_str(), &attr);
    DateModified = attr.st_ctime;

    HasIconPreview = false;
    IconPreview = nullptr;
    IconPreviewData = nullptr;
    IconPreviewHeight = 0;
    IconPreviewWidth = 0;
  }

  FileDialog::FileDialog() {
    m_isOpen = false;
    m_type = 0;
    m_calledOpenPopup = false;
    m_sortColumn = 0;
    m_sortDirection = ImGuiSortDirection_Ascending;
    m_filterSelection = 0;
    m_inputTextbox[0] = 0;
    m_pathBuffer[0] = 0;
    m_searchBuffer[0] = 0;
    m_newEntryBuffer[0] = 0;
    m_selectedFileItem = -1;
    m_zoom = 1.0f;

    m_previewLoader = nullptr;
    m_previewLoaderRunning = false;

    m_setDirectory(ghc::filesystem::current_path(), false);

    // favorites are available on every OS
    FileTreeNode* quickAccess = new FileTreeNode(IFD_QUICK_ACCESS);
    quickAccess->Read = true;
    m_treeCache.push_back(quickAccess);

    #ifdef _WIN32
    std::error_code ec;

    // Quick Access
    wchar_t userProfile[32767];
    ghc::filesystem::path homePath;
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, 32767)) 
      homePath = userProfile;
    if (ghc::filesystem::exists(homePath, ec))
      quickAccess->Children.push_back(new FileTreeNode(homePath));
    if (ngs::fs::environment_get_variable("IMGUI_CONFIG_PATH").empty())
      ngs::fs::environment_set_variable("IMGUI_CONFIG_PATH", homePath.string() + "\\.config\\filedialogs");
    if (ngs::fs::environment_get_variable("IMGUI_CONFIG_FILE").empty())
      ngs::fs::environment_set_variable("IMGUI_CONFIG_FILE", "filedialogs.txt");
    std::string conf = ngs::fs::environment_get_variable("IMGUI_CONFIG_PATH") + "\\" + ngs::fs::environment_get_variable("IMGUI_CONFIG_FILE");
    if (ngs::fs::file_exists(conf)) {
      int fd = ngs::fs::file_text_open_read(conf);
      if (fd != -1) {
        while (!ngs::fs::file_text_eof(fd)) {
          std::string fav = ngs::fs::filename_canonical(ngs::fs::file_text_read_string(fd));
          while (!fav.empty() && std::count(fav.begin(), fav.end(), '\\') > 1 && fav.back() == '\\') {
            fav.pop_back();
          }
          FileDialog::AddFavorite(fav);
          ngs::fs::file_text_readln(fd);
        }
        ngs::fs::file_text_close(fd);
      }
    }

    // This PC
    FileTreeNode* thisPC = new FileTreeNode(IFD_THIS_PC);
    thisPC->Read = true;
    DWORD d = GetLogicalDrives();
    for (int i = 0; i < 26; i++)
      if ((d & (1 << i)) && GetFileAttributesA((std::string(1, 'A' + i) + ":\\").c_str()) != -1)
        thisPC->Children.push_back(new FileTreeNode(std::string(1, 'A' + i) + ":\\"));
    m_treeCache.push_back(thisPC);
    #else
    std::error_code ec;
    
    // Quick Access
    ghc::filesystem::path homePath = getenv("HOME") ? getenv("HOME") : "";
    if (ghc::filesystem::exists(homePath, ec))
      quickAccess->Children.push_back(new FileTreeNode(homePath));
    if (ngs::fs::environment_get_variable("IMGUI_CONFIG_PATH").empty())
      ngs::fs::environment_set_variable("IMGUI_CONFIG_PATH", homePath.string() + "/.config/filedialogs");
    if (ngs::fs::environment_get_variable("IMGUI_CONFIG_FILE").empty())
      ngs::fs::environment_set_variable("IMGUI_CONFIG_FILE", "filedialogs.txt");
    std::string conf = ngs::fs::environment_get_variable("IMGUI_CONFIG_PATH") + "/" + ngs::fs::environment_get_variable("IMGUI_CONFIG_FILE");
    if (ngs::fs::file_exists(conf)) {
      int fd = ngs::fs::file_text_open_read(conf);
      if (fd != -1) {
        while (!ngs::fs::file_text_eof(fd)) {
          std::string fav = ngs::fs::filename_canonical(ngs::fs::file_text_read_string(fd));
          while (!fav.empty() && std::count(fav.begin(), fav.end(), '/' ) > 1 && fav.back() == '/' ) {
            fav.pop_back();
          }
          ngs::fs::file_text_readln(fd);
        }
        ngs::fs::file_text_close(fd);
      }
    }

    // This PC
    FileTreeNode* thisPC = new FileTreeNode(IFD_THIS_PC);
    thisPC->Read = true;
    for (const auto& entry : ghc::filesystem::directory_iterator("/", ec)) {
      const std::string& filename = entry.path().filename().string();
      #if !defined(_WIN32)
      const bool& is_hidden = ((!filename.empty()) ? (filename[0] == '.') : true);
      #else
      const bool& is_hidden = ((GetFileAttributesW(entry.path().wstring().c_str()) & FILE_ATTRIBUTE_HIDDEN) ||
        (GetFileAttributesW(entry.path().wstring().c_str()) & FILE_ATTRIBUTE_SYSTEM) || ((!filename.empty()) ? (filename[0] == '.') : true));
      #endif
      if (is_hidden) { continue; }
      if (ghc::filesystem::is_directory(entry, ec))
        thisPC->Children.push_back(new FileTreeNode(entry.path().string()));
    }
    m_treeCache.push_back(thisPC);
    #endif
  }

  FileDialog::~FileDialog() {
    m_clearIconPreview();
    m_clearIcons();

    for (auto fn : m_treeCache)
      m_clearTree(fn);
    m_treeCache.clear();
  }

  bool FileDialog::Save(const std::string& key, const std::string& title, const std::string& filter, const std::string& startingFile, const std::string& startingDir) {
    if (!m_currentKey.empty())
      return false;

    m_currentKey = key;
    m_currentTitle = title + "###" + key;
    m_isOpen = true;
    m_calledOpenPopup = false;
    m_result.clear();
    m_inputTextbox[0] = 0;
    m_selections.clear();
    m_selectedFileItem = -1;
    m_isMultiselect = false;
    m_type = IFD_DIALOG_SAVE;

    strcpy(m_inputTextbox, startingFile.substr(0, 1023).c_str());

    m_parseFilter(filter);
    if (!startingDir.empty())
      m_setDirectory(ghc::filesystem::path(startingDir), false, false);
    else
      m_setDirectory(m_currentDirectory, false, false); // refresh contents

    return true;
  }

  bool FileDialog::Open(const std::string& key, const std::string& title, const std::string& filter, bool isMultiselect, const std::string& startingFile, const std::string& startingDir) {
    if (!m_currentKey.empty())
      return false;

    m_currentKey = key;
    m_currentTitle = title + "###" + key;
    m_isOpen = true;
    m_calledOpenPopup = false;
    m_result.clear();
    m_inputTextbox[0] = 0;
    m_selections.clear();
    m_selectedFileItem = -1;
    m_isMultiselect = isMultiselect;
    m_type = filter.empty() ? IFD_DIALOG_DIRECTORY : IFD_DIALOG_FILE;

    strcpy(m_inputTextbox, startingFile.substr(0, 1023).c_str());

    m_parseFilter(filter);
    if (!startingDir.empty())
      m_setDirectory(ghc::filesystem::path(startingDir), false, false);
    else
      m_setDirectory(m_currentDirectory, false, false); // refresh contents

    return true;
  }

  bool FileDialog::IsDone(const std::string& key) {
    bool isMe = m_currentKey == key;

    if (isMe && m_isOpen) {
      if (!m_calledOpenPopup) {
        ImGui::SetNextWindowSize(ImVec2((float)IFD_DIALOG_WIDTH, (float)IFD_DIALOG_HEIGHT), 0);
        ImGui::OpenPopup(m_currentTitle.c_str());
        m_calledOpenPopup = true;
      }

      if (ImGui::BeginPopupModal(m_currentTitle.c_str(), &m_isOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        m_renderFileDialog();
        ImGui::EndPopup();
      }
      else m_isOpen = false;
    }

    return isMe && !m_isOpen;
  }

  void FileDialog::Close() {
    std::error_code ec;
    if (!ngs::fs::directory_exists("${IMGUI_CONFIG_PATH}"))
      ngs::fs::directory_create("${IMGUI_CONFIG_PATH}");
    #if defined(_WIN32)
    int fd = ngs::fs::file_text_open_write("${IMGUI_CONFIG_PATH}\\${IMGUI_CONFIG_FILE}");
    #else
    int fd = ngs::fs::file_text_open_write("${IMGUI_CONFIG_PATH}/${IMGUI_CONFIG_FILE}");
    #endif
    if (fd != -1) {
      for (auto& p : m_treeCache) {
        if (p->Path == IFD_QUICK_ACCESS) {
          for (size_t i = 0; i < p->Children.size(); i++) {
            if (ghc::filesystem::exists(p->Children[i]->Path.string(), ec)) {
              ngs::fs::file_text_write_string(fd, p->Children[i]->Path.string());
              ngs::fs::file_text_writeln(fd);
            }
          }
        }
      }
      ngs::fs::file_text_close(fd);
    }
  
    m_currentKey.clear();
    m_backHistory = std::stack<ghc::filesystem::path>();
    m_forwardHistory = std::stack<ghc::filesystem::path>();

    // clear the tree
    for (auto fn : m_treeCache) {
      for (auto item : fn->Children) {
        for (auto ch : item->Children)
          m_clearTree(ch);
        item->Children.clear();
        item->Read = false;
      }
    }

    // free icon textures
    m_clearIconPreview();
    m_clearIcons();
  }

  void FileDialog::RemoveFavorite(const std::string& path) {
    auto itr = std::find(m_favorites.begin(), m_favorites.end(), m_currentDirectory.string());

    if (itr != m_favorites.end())
      m_favorites.erase(itr);

    // remove from sidebar
    for (auto& p : m_treeCache)
      if (p->Path == IFD_QUICK_ACCESS) {
        for (size_t i = 0; i < p->Children.size(); i++)
          if (p->Children[i]->Path == path) {
            p->Children.erase(p->Children.begin() + i);
            break;
          }
        break;
      }
  }

  void FileDialog::AddFavorite(const std::string& path) {
    if (std::count(m_favorites.begin(), m_favorites.end(), path) > 0)
      return;

    if (!ghc::filesystem::exists(ghc::filesystem::path(path)))
      return;

    #ifdef _WIN32
    wchar_t userProfile[32767];
    ghc::filesystem::path homePath;
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, 32767)) 
      homePath = userProfile;
    #else
    ghc::filesystem::path homePath = getenv("HOME") ? getenv("HOME") : "";
    #endif

    if (path == homePath.string())
      return; 

    m_favorites.push_back(path);
    
    // add to sidebar
    for (auto& p : m_treeCache)
      if (p->Path == IFD_QUICK_ACCESS) {
        p->Children.push_back(new FileTreeNode(path));
        break;
      }
  }
  
  void FileDialog::m_select(const ghc::filesystem::path& path, bool isCtrlDown) {
    bool multiselect = isCtrlDown && m_isMultiselect;

    if (!multiselect) {
      m_selections.clear();
      m_selections.push_back(path);
    } else {
      auto it = std::find(m_selections.begin(), m_selections.end(), path);
      if (it != m_selections.end())
        m_selections.erase(it);
      else
        m_selections.push_back(path);
    }

    if (m_selections.size() == 1) {
      std::string filename = m_selections[0].filename().string();
      if (filename.size() == 0)
        filename = m_selections[0].string(); // drive

      strcpy(m_inputTextbox, filename.c_str());
    } else {
      std::string textboxVal = "";
      for (const auto& sel : m_selections) {
        std::string filename = sel.filename().string();
        if (filename.size() == 0)
          filename = sel.string();

        textboxVal += "\"" + filename + "\", ";
      }
      strcpy(m_inputTextbox, textboxVal.substr(0, textboxVal.size() - 2).c_str());
    }
  }

  bool FileDialog::m_finalize(const std::string& filename) {
    bool hasResult = (!filename.empty() && m_type != IFD_DIALOG_DIRECTORY) || m_type == IFD_DIALOG_DIRECTORY;
    
    if (hasResult) {
      if (!m_isMultiselect || m_selections.size() <= 1) {
        ghc::filesystem::path path = ghc::filesystem::path(filename);
        if (path.is_absolute()) m_result.push_back(path);
        else m_result.push_back(m_currentDirectory / path);
        if (m_type == IFD_DIALOG_DIRECTORY || m_type == IFD_DIALOG_FILE) {
          if (!ghc::filesystem::exists(m_result.back())) {
            m_result.clear();
            return false;
          }
        }
      } else {
        for (const auto& sel : m_selections) {
          if (sel.is_absolute()) m_result.push_back(sel);
          else m_result.push_back(m_currentDirectory / sel);
          if (m_type == IFD_DIALOG_DIRECTORY || m_type == IFD_DIALOG_FILE) {
            if (!ghc::filesystem::exists(m_result.back())) {
              m_result.clear();
              return false;
            }
          }
        }
      }
      
      if (m_type == IFD_DIALOG_SAVE) {
        // add the extension
        if (m_filterSelection < m_filterExtensions.size() && m_filterExtensions[m_filterSelection].size() > 0) {
          if (!m_result.back().has_extension()) {
            std::string extAdd = m_filterExtensions[m_filterSelection][0];
            m_result.back().replace_extension(extAdd);
          }
        }
      }
    }

    std::error_code ec;
    if (!m_result.empty() && m_type == IFD_DIALOG_SAVE && 
      !ghc::filesystem::exists(m_result.back(), ec) && !ghc::filesystem::is_directory(m_result.back(), ec)) {
      m_isOpen = false;
      return true;
    } else if (!m_result.size() && m_type == IFD_DIALOG_SAVE && filename.empty()) {
      m_isOpen = false;
      return true;
    } else if (m_result.size() && m_type == IFD_DIALOG_FILE && 
      ghc::filesystem::exists(m_result.back(), ec) && !ghc::filesystem::is_directory(m_result.back(), ec)) {
      m_isOpen = false;
      return true;
    } else if (m_result.size() && m_type == IFD_DIALOG_DIRECTORY && 
      ghc::filesystem::exists(m_result.back(), ec) && ghc::filesystem::is_directory(m_result.back(), ec)) {
      m_isOpen = false;
      return true;
    }

    return false;
  }

  void FileDialog::m_parseFilter(const std::string& filter) {
    m_filter = "";
    m_filterExtensions.clear();
    m_filterSelection = 0;

    if (filter.empty())
      return;

    std::vector<std::string> exts;

    size_t lastSplit = 0, lastExt = 0;
    bool inExtList = false;
    for (size_t i = 0; i < filter.size(); i++) {
      if (filter[i] == ',') {
        if (!inExtList)
          lastSplit = i + 1;
        else {
          exts.push_back(filter.substr(lastExt, i - lastExt));
          lastExt = i + 1;
        }
      }
      else if (filter[i] == '{') {
        std::string filterName = filter.substr(lastSplit, i - lastSplit);
        if (filterName == "*.*" || filterName == ".*" || filterName == "*") {
          m_filter += std::string((IFD_ALL_FILES + std::string("\0")).c_str(), strlen(IFD_ALL_FILES) + 1);
          m_filterExtensions.push_back(std::vector<std::string>());
        }
        else
          m_filter += std::string((filterName + "\0").c_str(), filterName.size() + 1);
        inExtList = true;
        lastExt = i + 1;
      }
      else if (filter[i] == '}') {
        exts.push_back(filter.substr(lastExt, i - lastExt));
        m_filterExtensions.push_back(exts);
        exts.clear();

        inExtList = false;
      }
    }
    if (lastSplit != 0) {
      std::string filterName = filter.substr(lastSplit);
      if (filterName == "*.*" || filterName == ".*" || filterName == "*") {
        m_filter += std::string((IFD_ALL_FILES + std::string("\0")).c_str(), strlen(IFD_ALL_FILES) + 1);
        m_filterExtensions.push_back(std::vector<std::string>());
      }
      else
        m_filter += std::string((filterName + "\0").c_str(), filterName.size() + 1);
    }
  }

  void* FileDialog::m_getIcon(const ghc::filesystem::path& path) {
#ifdef _WIN32
    if (m_icons.count(path.string()) > 0)
      return m_icons[path.string()];

    std::string pathU8 = path.string();

    std::error_code ec;
    m_icons[pathU8] = nullptr;

    DWORD attrs = 0;
    UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
    if (!ghc::filesystem::exists(path, ec)) {
      flags |= SHGFI_USEFILEATTRIBUTES;
      attrs = FILE_ATTRIBUTE_DIRECTORY;
    }

    SHFILEINFOW fileInfo = { 0 };
    std::wstring pathW = path.wstring();
    for (unsigned i = 0; i < pathW.size(); i++)
      if (pathW[i] == '/')
        pathW[i] = '\\';
    SHGetFileInfoW(pathW.c_str(), attrs, &fileInfo, sizeof(SHFILEINFOW), flags);

    if (fileInfo.hIcon == nullptr)
      return nullptr;

    // check if icon is already loaded
    auto itr = std::find(m_iconIndices.begin(), m_iconIndices.end(), fileInfo.iIcon);
    if (itr != m_iconIndices.end()) {
      const std::string& existingIconFilepath = m_iconFilepaths[itr - m_iconIndices.begin()];
      m_icons[pathU8] = m_icons[existingIconFilepath];
      return m_icons[pathU8];
    }

    m_iconIndices.push_back(fileInfo.iIcon);
    m_iconFilepaths.push_back(pathU8);

    ICONINFO iconInfo = { 0 };
    GetIconInfo(fileInfo.hIcon, &iconInfo);
    
    if (iconInfo.hbmColor == nullptr)
      return nullptr;

    DIBSECTION ds;
    GetObject(iconInfo.hbmColor, sizeof(ds), &ds);
    int byteSize = ds.dsBm.bmWidth * ds.dsBm.bmHeight * (ds.dsBm.bmBitsPixel / 8);

    if (byteSize == 0)
      return nullptr;

    uint8_t* data = (uint8_t*)malloc(byteSize);
    GetBitmapBits(iconInfo.hbmColor, byteSize, data);

    m_icons[pathU8] = this->CreateTexture(data, ds.dsBm.bmWidth, ds.dsBm.bmHeight, 0);

    free(data);

    return m_icons[pathU8];
    #else
    if (m_icons.count(path.string()) > 0)
      return m_icons[path.string()];

    std::string pathU8 = path.string();

    m_icons[pathU8] = nullptr;

    std::error_code ec;
    int iconID = 1;
    if (ghc::filesystem::is_directory(path, ec))
      iconID = 0;

    // check if icon is already loaded
    auto itr = std::find(m_iconIndices.begin(), m_iconIndices.end(), iconID);
    if (itr != m_iconIndices.end()) {
      const std::string& existingIconFilepath = m_iconFilepaths[itr - m_iconIndices.begin()];
      m_icons[pathU8] = m_icons[existingIconFilepath];
      return m_icons[pathU8];
    }

    m_iconIndices.push_back(iconID);
    m_iconFilepaths.push_back(pathU8);

    ImVec4 wndBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

    // light theme - load default icons
    if ((wndBg.x + wndBg.y + wndBg.z) / 3.0f > 0.5f) {
      uint8_t* data = (uint8_t*)ifd::GetDefaultFileIcon();
      if (iconID == 0)
        data = (uint8_t*)ifd::GetDefaultFolderIcon();
      m_icons[pathU8] = this->CreateTexture(data, DEFAULT_ICON_SIZE, DEFAULT_ICON_SIZE, 0);
    }
    // dark theme - invert the colors
    else {
      uint8_t* data = (uint8_t*)ifd::GetDefaultFileIcon();
      if (iconID == 0)
        data = (uint8_t*)ifd::GetDefaultFolderIcon();

      uint8_t* invData = (uint8_t*)malloc(DEFAULT_ICON_SIZE * DEFAULT_ICON_SIZE * 4);
      for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
          int index = (y* DEFAULT_ICON_SIZE + x) * 4;
          invData[index + 0] = 255 - data[index + 0];
          invData[index + 1] = 255 - data[index + 1];
          invData[index + 2] = 255 - data[index + 2];
          invData[index + 3] = data[index + 3];
        }
      }
      m_icons[pathU8] = this->CreateTexture(invData, DEFAULT_ICON_SIZE, DEFAULT_ICON_SIZE, 0);

      free(invData);
    }

    return m_icons[pathU8];
    #endif
  }

  void FileDialog::m_clearIcons() {
    std::vector<unsigned int> deletedIcons;

    // delete textures
    for (auto& icon : m_icons) {
      unsigned int ptr = (unsigned int)((uintptr_t)icon.second);
      if (std::count(deletedIcons.begin(), deletedIcons.end(), ptr)) // skip duplicates
        continue;

      deletedIcons.push_back(ptr);
      DeleteTexture(icon.second);
    }
    m_iconFilepaths.clear();
    m_iconIndices.clear();
    m_icons.clear();
  }

  void FileDialog::m_refreshIconPreview() {
    if (m_zoom >= 5.0f) {
      if (m_previewLoader == nullptr) {
        m_previewLoaderRunning = true;
        m_previewLoader = new std::thread(&FileDialog::m_loadPreview, this);
      }
    } else
      m_clearIconPreview();
  }

  void FileDialog::m_clearIconPreview() {
    m_stopPreviewLoader();

    for (auto& data : m_content) {
      if (!data.HasIconPreview)
        continue;

      data.HasIconPreview = false;
      this->DeleteTexture(data.IconPreview);

      if (data.IconPreviewData != nullptr) {
        stbi_image_free(data.IconPreviewData);
        data.IconPreviewData = nullptr;
      }
    }
  }

  void FileDialog::m_stopPreviewLoader() {
    if (m_previewLoader != nullptr) {
      m_previewLoaderRunning = false;

      if (m_previewLoader && m_previewLoader->joinable())
        m_previewLoader->join();

      delete m_previewLoader;
      m_previewLoader = nullptr;
    }
  }

  void FileDialog::m_loadPreview() {
    for (size_t i = 0; m_previewLoaderRunning && i < m_content.size(); i++) {
      auto& data = m_content[i];

      if (data.HasIconPreview)
        continue;

      if (data.Path.has_extension()) {
        std::string ext = data.Path.extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
          int width, height, nrChannels;
          unsigned char* image = stbi_load(data.Path.string().c_str(), &width, &height, &nrChannels, STBI_rgb_alpha);

          if (image == nullptr || width == 0 || height == 0)
            continue;

          data.HasIconPreview = true;
          data.IconPreviewData = image;
          data.IconPreviewWidth = width;
          data.IconPreviewHeight = height;
        }
      }
    }

    m_previewLoaderRunning = false;
  }

  void FileDialog::m_clearTree(FileTreeNode* node) {
    if (node == nullptr)
      return;

    for (auto n : node->Children)
      m_clearTree(n);

    delete node;
    node = nullptr;
  }

  void FileDialog::m_setDirectory(const ghc::filesystem::path& p, bool addHistory, bool clearFileName) {
    bool isSameDir = m_currentDirectory == p;

    if (addHistory && !isSameDir)
      m_backHistory.push(m_currentDirectory);

    m_currentDirectory = p;
    #ifdef _WIN32
    while (!m_currentDirectory.string().empty() && m_currentDirectory.string().length() > 3 && m_currentDirectory.string().back() == '\\')
      m_currentDirectory = m_currentDirectory.string().substr(0, m_currentDirectory.string().length() - 1);

    // drives don't work well without the backslash symbol
    if (p.string().size() == 2 && p.string()[1] == ':')
      m_currentDirectory = ghc::filesystem::path(p.string() + "\\");
    #else
    while (!m_currentDirectory.string().empty() && m_currentDirectory.string().length() > 1 && m_currentDirectory.string().back() == '/')
      m_currentDirectory = m_currentDirectory.string().substr(0, m_currentDirectory.string().length() - 1);
    #endif

    m_clearIconPreview();
    m_content.clear(); // p == "" after this line, due to reference
    m_selectedFileItem = -1;
    
    if ((m_type == IFD_DIALOG_DIRECTORY || m_type == IFD_DIALOG_FILE) && clearFileName)
      m_inputTextbox[0] = 0;
    m_selections.clear();

    if (!isSameDir) {
      m_searchBuffer[0] = 0;
      m_clearIcons();
    }

    if (p.string() == IFD_QUICK_ACCESS) {
      for (auto& node : m_treeCache) {
        if (node->Path == p)
          for (auto& c : node->Children)
            m_content.push_back(FileData(c->Path));
      }
    } 
    else if (p.string() == IFD_THIS_PC) {
      for (auto& node : m_treeCache) {
        if (node->Path == p)
          for (auto& c : node->Children)
            m_content.push_back(FileData(c->Path));
      }
    } else {
      std::error_code ec;
      if (ghc::filesystem::exists(m_currentDirectory, ec))
        for (const auto& entry : ghc::filesystem::directory_iterator(m_currentDirectory, ec)) {
          const std::string& filename = entry.path().filename().string();
          #if !defined(_WIN32)
          const bool& is_hidden = ((!filename.empty()) ? (filename[0] == '.') : true);
          #else
          const bool& is_hidden = ((GetFileAttributesW(entry.path().wstring().c_str()) & FILE_ATTRIBUTE_HIDDEN) ||
            (GetFileAttributesW(entry.path().wstring().c_str()) & FILE_ATTRIBUTE_SYSTEM) || ((!filename.empty()) ? (filename[0] == '.') : true));
          #endif
          if (is_hidden) { continue; }
          FileData info(entry.path());

          // skip files when IFD_DIALOG_DIRECTORY
          if (!info.IsDirectory && m_type == IFD_DIALOG_DIRECTORY)
            continue;

          // check if filename matches search query
          if (m_searchBuffer[0]) {
            std::string filename = info.Path.string();

            std::string filenameSearch = filename;
            std::string query(m_searchBuffer);
            std::transform(filenameSearch.begin(), filenameSearch.end(), filenameSearch.begin(), ::tolower);
            std::transform(query.begin(), query.end(), query.begin(), ::tolower);

            if (filenameSearch.find(query, 0) == std::string::npos)
              continue;
          }

          // check if extension matches
          if (!info.IsDirectory && m_type != IFD_DIALOG_DIRECTORY) {
            if (m_filterSelection < m_filterExtensions.size()) {
              const auto& exts = m_filterExtensions[m_filterSelection];
              if (exts.size() > 0) {
                std::string extension = info.Path.extension().string();

                // extension not found? skip
                if (std::count(exts.begin(), exts.end(), extension) == 0)
                  continue;
              }
            }
          }

          m_content.push_back(info);
        }
    }

    m_sortContent(m_sortColumn, m_sortDirection);
    m_refreshIconPreview();
  }

  void FileDialog::m_sortContent(unsigned int column, unsigned int sortDirection) {
    // 0 -> name, 1 -> date, 2 -> size
    m_sortColumn = column;
    m_sortDirection = sortDirection;

    // split into directories and files
    std::partition(m_content.begin(), m_content.end(), [](const FileData& data) {
      return data.IsDirectory;
    });

    if (m_content.size() > 0) {
      // find where the file list starts
      size_t fileIndex = 0;
      for (; fileIndex < m_content.size(); fileIndex++)
        if (!m_content[fileIndex].IsDirectory)
          break;

      // compare function
      auto compareFn = [column, sortDirection](const FileData& left, const FileData& right) -> bool {
        // name
        if (column == 0) {
          std::string lName = left.Path.string();
          std::string rName = right.Path.string();

          std::transform(lName.begin(), lName.end(), lName.begin(), ::tolower);
          std::transform(rName.begin(), rName.end(), rName.begin(), ::tolower);

          int comp = lName.compare(rName);

          if (sortDirection == ImGuiSortDirection_Ascending)
            return comp < 0;
          return comp > 0;
        }
        // date
        else if (column == 1) {
          if (sortDirection == ImGuiSortDirection_Ascending)
            return left.DateModified < right.DateModified;
          else
            return left.DateModified > right.DateModified;
        }
        // size
        else if (column == 2) {
          if (sortDirection == ImGuiSortDirection_Ascending)
            return left.Size < right.Size;
          else
            return left.Size > right.Size;
        }

        return false;
      };

      // sort the directories
      std::sort(m_content.begin(), m_content.begin() + fileIndex, compareFn);

      // sort the files
      std::sort(m_content.begin() + fileIndex, m_content.end(), compareFn);
    }
  }

  void FileDialog::m_renderTree(FileTreeNode* node) {
    // directory
    std::error_code ec;
    ImGui::PushID(node);
    bool isClicked = false;
    std::string displayName = node->Path.stem().string();
    if (displayName.size() == 0)
      displayName = node->Path.string();
    if (FolderNode(displayName.c_str(), (ImTextureID)m_getIcon(node->Path), isClicked)) {
      if (!node->Read) {
        // cache children if it's not already cached
        if (ghc::filesystem::exists(node->Path, ec))
          for (const auto& entry : ghc::filesystem::directory_iterator(node->Path, ec)) {
          const std::string& filename = entry.path().filename().string();
          #if !defined(_WIN32)
          const bool& is_hidden = ((!filename.empty()) ? (filename[0] == '.') : true);
          #else
          const bool& is_hidden = ((GetFileAttributesW(entry.path().wstring().c_str()) & FILE_ATTRIBUTE_HIDDEN) ||
            (GetFileAttributesW(entry.path().wstring().c_str()) & FILE_ATTRIBUTE_SYSTEM) || ((!filename.empty()) ? (filename[0] == '.') : true));
          #endif
          if (is_hidden) { continue; }
            if (ghc::filesystem::is_directory(entry, ec))
              node->Children.push_back(new FileTreeNode(entry.path().string()));
          }
        node->Read = true;
      }

      // display children
      for (auto c : node->Children)
        m_renderTree(c);

      ImGui::TreePop();
    }
    if (isClicked)
      m_setDirectory(node->Path);
    ImGui::PopID();
  }

  void FileDialog::m_renderContent() {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      m_selectedFileItem = -1;

    // table view
    if (m_zoom == 1.0f) {
      if (ImGui::BeginTable("##contentTable", 3, /*ImGuiTableFlags_Resizable |*/ ImGuiTableFlags_Sortable, ImVec2(0, -FLT_MIN))) {
        // header
        ImGui::TableSetupColumn((IFD_NAME + std::string("##filename")).c_str(), ImGuiTableColumnFlags_WidthStretch, 0.0f -1.0f, 0);
        ImGui::TableSetupColumn((IFD_DATE_MODIFIED + std::string("##filedate")).c_str(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 0.0f, 1);
        ImGui::TableSetupColumn((IFD_SIZE + std::string("##filesize")).c_str(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 0.0f, 2);
                ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // sort
        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                    if (sortSpecs->SpecsDirty) {
            sortSpecs->SpecsDirty = false;
            m_sortContent(sortSpecs->Specs->ColumnUserID, sortSpecs->Specs->SortDirection);
                    }
        }

        // content
        int fileId = 0;
        for (auto& entry : m_content) {
          std::string filename = entry.Path.filename().string();
          if (filename.size() == 0)
            filename = entry.Path.string(); // drive
          
          bool isSelected = std::count(m_selections.begin(), m_selections.end(), entry.Path);

          ImGui::TableNextRow();

          // file name
          ImGui::TableSetColumnIndex(0);
          ImGui::Image((ImTextureID)m_getIcon(entry.Path), ImVec2(ICON_SIZE, ICON_SIZE));
          ImGui::SameLine();
          if (ImGui::Selectable(filename.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
            std::error_code ec;
            bool isDir = ghc::filesystem::is_directory(entry.Path, ec);

            if (ImGui::IsMouseDoubleClicked(0)) {
              if (isDir) {
                m_setDirectory(entry.Path);
                break;
              } else
                m_finalize(filename);
            } else {
              if ((isDir && m_type == IFD_DIALOG_DIRECTORY) || !isDir)
                m_select(entry.Path, ImGui::GetIO().KeyCtrl);
            }
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            m_selectedFileItem = fileId;
          fileId++;

          // date
          ImGui::TableSetColumnIndex(1);
          auto tm = std::localtime(&entry.DateModified);
          if (tm != nullptr)
            ImGui::Text("%d/%d/%d %02d:%02d", tm->tm_mon + 1, tm->tm_mday, 1900 + tm->tm_year, tm->tm_hour, tm->tm_min);
          else ImGui::Text("---");

          // size
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("%.3f KiB", entry.Size/1024.0f);
        }

        ImGui::EndTable();
      }
    }
    // "icon" view
    else {
      // content
      int fileId = 0;
      for (auto& entry : m_content) {
        if (entry.HasIconPreview && entry.IconPreviewData != nullptr) {
          entry.IconPreview = this->CreateTexture(entry.IconPreviewData, entry.IconPreviewWidth, entry.IconPreviewHeight, 1);
          stbi_image_free(entry.IconPreviewData);
          entry.IconPreviewData = nullptr;
        }

        std::string filename = entry.Path.filename().string();
        if (filename.size() == 0)
          filename = entry.Path.string(); // drive

        bool isSelected = std::count(m_selections.begin(), m_selections.end(), entry.Path);

        if (FileIcon(filename.c_str(), isSelected, entry.HasIconPreview ? entry.IconPreview : (ImTextureID)m_getIcon(entry.Path), ImVec2(32 + 16 * m_zoom, 32 + 16 * m_zoom), entry.HasIconPreview, entry.IconPreviewWidth, entry.IconPreviewHeight)) {
          std::error_code ec;
          bool isDir = ghc::filesystem::is_directory(entry.Path, ec);

          if (ImGui::IsMouseDoubleClicked(0)) {
            if (isDir) {
              m_setDirectory(entry.Path);
              break;
            }
            else
              m_finalize(filename);
          }
          else {
            if ((isDir && m_type == IFD_DIALOG_DIRECTORY) || !isDir)
              m_select(entry.Path, ImGui::GetIO().KeyCtrl);
          }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
          m_selectedFileItem = fileId;
        fileId++;
      }
    }
  }

  void FileDialog::m_renderPopups() {
    bool openAreYouSureDlg = false, openNewFileDlg = false, openNewDirectoryDlg = false, openOverwriteDlg = false;
    if (ImGui::BeginPopupContextItem("##dir_context")) {
      if (ImGui::Selectable(IFD_NEW_FILE))
        openNewFileDlg = true;
      if (ImGui::Selectable(IFD_NEW_DIRECTORY))
        openNewDirectoryDlg = true;
      if (m_selectedFileItem != -1 && ImGui::Selectable(IFD_DELETE))
        openAreYouSureDlg = true;
      ImGui::EndPopup();
    }
    std::error_code ec;
    if (m_result.size() && m_isOpen && strlen(m_inputTextbox) && m_type == IFD_DIALOG_SAVE && 
      ghc::filesystem::exists(m_currentDirectory / m_inputTextbox, ec) && !ghc::filesystem::is_directory(m_currentDirectory / m_inputTextbox, ec))
      openOverwriteDlg = true;
    if (m_result.size() && m_isOpen && strlen(m_inputTextbox) && m_type != IFD_DIALOG_DIRECTORY && 
      ghc::filesystem::exists(m_currentDirectory / m_inputTextbox, ec) && ghc::filesystem::is_directory(m_currentDirectory / m_inputTextbox, ec)) {
      m_setDirectory(m_currentDirectory / m_inputTextbox, false);
      m_inputTextbox[0] = 0;
      m_result.clear();
    }
    if (openAreYouSureDlg)
      ImGui::OpenPopup((IFD_ARE_YOU_SURE + std::string("##delete")).c_str());
    if (openOverwriteDlg)
      ImGui::OpenPopup((IFD_OVERWRITE_FILE + std::string("##overwrite")).c_str());
    if (openNewFileDlg)
      ImGui::OpenPopup((IFD_ENTER_FILE_NAME + std::string("##newfile")).c_str());
    if (openNewDirectoryDlg)
      ImGui::OpenPopup((IFD_ENTER_DIRECTORY_NAME + std::string("##newdir")).c_str());
    ImGui::SetNextWindowSize(ImVec2((float)((GImGui->FontSize * strlen(IFD_ARE_YOU_SURE_YOU_WANT_TO_DELETE)) / 2.85), (float)(GImGui->FontSize * 6)), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal((IFD_ARE_YOU_SURE + std::string("##delete")).c_str())) {
      if (m_selectedFileItem >= static_cast<int>(m_content.size()) || m_content.size() == 0)
        ImGui::CloseCurrentPopup();
      else {
        const FileData& data = m_content[m_selectedFileItem];
        ImGui::TextWrapped(IFD_ARE_YOU_SURE_YOU_WANT_TO_DELETE, data.Path.filename().string().c_str());
        if (ImGui::Button(IFD_YES)) {
          std::error_code ec;
          ghc::filesystem::remove_all(data.Path, ec);
          m_setDirectory(m_currentDirectory, false); // refresh
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(IFD_NO))
          ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize(ImVec2((float)((GImGui->FontSize * strlen(IFD_ARE_YOU_SURE_YOU_WANT_TO_OVERWRITE)) / 2.85), (float)(GImGui->FontSize * 6)), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal((IFD_OVERWRITE_FILE + std::string("##overwrite")).c_str())) {
      if (m_selectedFileItem >= static_cast<int>(m_content.size()) || m_content.size() == 0)
        ImGui::CloseCurrentPopup();
      else {
        const FileData& data = m_content[m_selectedFileItem];
        ImGui::TextWrapped(IFD_ARE_YOU_SURE_YOU_WANT_TO_OVERWRITE, m_inputTextbox);
        if (ImGui::Button(IFD_YES)) {
          m_isOpen = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(IFD_NO)) {
          m_result.clear();
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal((IFD_ENTER_FILE_NAME + std::string("##newfile")).c_str())) {
      ImGui::PushItemWidth(250.0f);
      ImGui::InputText("##newfilename", m_newEntryBuffer, 1024); // TODO: remove hardcoded literals
      ImGui::PopItemWidth();

      if (ImGui::Button(IFD_OK)) {
        std::ofstream out((m_currentDirectory / std::string(m_newEntryBuffer)).string());
        out << "";
        out.close();

        m_setDirectory(m_currentDirectory, false); // refresh
        m_newEntryBuffer[0] = 0;

        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(IFD_CANCEL)) {
        m_newEntryBuffer[0] = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal((IFD_ENTER_DIRECTORY_NAME + std::string("##newdir")).c_str())) {
      ImGui::PushItemWidth(250.0f);
      ImGui::InputText("##newfilename", m_newEntryBuffer, 1024); // TODO: remove hardcoded literals
      ImGui::PopItemWidth();

      if (ImGui::Button(IFD_OK)) {
        std::error_code ec;
        ghc::filesystem::create_directory(m_currentDirectory / std::string(m_newEntryBuffer), ec);
        m_setDirectory(m_currentDirectory, false); // refresh
        m_newEntryBuffer[0] = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(IFD_CANCEL)) {
        ImGui::CloseCurrentPopup();
        m_newEntryBuffer[0] = 0;
      }
      ImGui::EndPopup();
    }
  }

  void FileDialog::m_renderFileDialog() {
    /***** TOP BAR *****/
    bool noBackHistory = m_backHistory.empty(), noForwardHistory = m_forwardHistory.empty();
    
    ImGui::PushStyleColor(ImGuiCol_Button, 0);
    if (noBackHistory) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    if (ImGui::ArrowButtonEx("##back", ImGuiDir_Left, ImVec2(GUI_ELEMENT_SIZE, GUI_ELEMENT_SIZE), m_backHistory.empty() * ImGuiItemFlags_Disabled)) {
      ghc::filesystem::path newPath = m_backHistory.top();
      m_backHistory.pop();
      m_forwardHistory.push(m_currentDirectory);

      m_setDirectory(newPath, false);
    }
    if (noBackHistory) ImGui::PopStyleVar();
    ImGui::SameLine();
    
    if (noForwardHistory) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    if (ImGui::ArrowButtonEx("##forward", ImGuiDir_Right, ImVec2(GUI_ELEMENT_SIZE, GUI_ELEMENT_SIZE), m_forwardHistory.empty() * ImGuiItemFlags_Disabled)) {
      ghc::filesystem::path newPath = m_forwardHistory.top();
      m_forwardHistory.pop();
      m_backHistory.push(m_currentDirectory);

      m_setDirectory(newPath, false);
    }
    if (noForwardHistory) ImGui::PopStyleVar();
    ImGui::SameLine();
    
    if (ImGui::ArrowButtonEx("##up", ImGuiDir_Up, ImVec2(GUI_ELEMENT_SIZE, GUI_ELEMENT_SIZE))) {
      if (m_currentDirectory.has_parent_path())
        m_setDirectory(m_currentDirectory.parent_path());
    }
    
    ghc::filesystem::path curDirCopy = m_currentDirectory;
    if (PathBox("##pathbox", curDirCopy, m_pathBuffer, ImVec2(-250, GUI_ELEMENT_SIZE)))
      m_setDirectory(curDirCopy);
    ImGui::SameLine();
    
    if (FavoriteButton("##dirfav", std::count(m_favorites.begin(), m_favorites.end(), m_currentDirectory.string()))) {
      if (std::count(m_favorites.begin(), m_favorites.end(), m_currentDirectory.string()))
        RemoveFavorite(m_currentDirectory.string());
      else 
        AddFavorite(m_currentDirectory.string());
    }
    ImGui::SameLine();
    ImGui::PopStyleColor();

    if (ImGui::InputTextEx("##searchTB", IFD_SEARCH, m_searchBuffer, 128, ImVec2(-FLT_MIN, GUI_ELEMENT_SIZE), 0)) // TODO: no hardcoded literals
      m_setDirectory(m_currentDirectory, false); // refresh



    /***** CONTENT *****/
    float bottomBarHeight = (GImGui->FontSize + ImGui::GetStyle().FramePadding.y + ImGui::GetStyle().ItemSpacing.y * 2.0f) * 2;
    if (ImGui::BeginTable("##table", 2, ImGuiTableFlags_Resizable, ImVec2(0, -bottomBarHeight))) {
      ImGui::TableSetupColumn("##tree", ImGuiTableColumnFlags_WidthFixed, (float)(GImGui->FontSize - ImGui::GetStyle().FramePadding.x / 2 + 
      ImGui::GetStyle().ItemSpacing.x) * 2 + (ImGui::GetColumnOffset(0) * 2) + ImGui::CalcTextSize(((ImGui::CalcTextSize(IFD_QUICK_ACCESS).x >= 
      ImGui::CalcTextSize(IFD_THIS_PC).x) ? IFD_QUICK_ACCESS : IFD_THIS_PC)).x); 
      ImGui::TableSetupColumn("##content", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();

      // the tree on the left side
      ImGui::TableSetColumnIndex(0);
      ImGui::BeginChild("##treeContainer", ImVec2(0, -bottomBarHeight));
      for (auto node : m_treeCache)
        m_renderTree(node);
      ImGui::EndChild();
      
      // content on the right side
      ImGui::TableSetColumnIndex(1);
      ImGui::BeginChild("##contentContainer", ImVec2(0, -bottomBarHeight));
        m_renderContent();
      ImGui::EndChild();
      if (ImGui::IsItemHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
        m_zoom = std::min<float>(25.0f, std::max<float>(1.0f, m_zoom + ImGui::GetIO().MouseWheel));
        m_refreshIconPreview();
      }

      // New file, New directory and Delete popups
      m_renderPopups();

      ImGui::EndTable();
    }


    
    /***** BOTTOM BAR *****/
    ImGui::Text(IFD_FILE_NAME_WITH_COLON);
    ImGui::SameLine();
    ghc::filesystem::path compare_with_root = m_currentDirectory / m_inputTextbox;
    if (ImGui::InputTextEx("##file_input", IFD_FILE_NAME_WITHOUT_COLON, m_inputTextbox, 1024, ImVec2((m_type != IFD_DIALOG_DIRECTORY) ? -250.0f : -FLT_MIN, 0), ImGuiInputTextFlags_EnterReturnsTrue) && 
      #if defined(_WIN32)
      ((m_type == IFD_DIALOG_SAVE) || (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(compare_with_root.wstring().c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)) &&
      #endif
      compare_with_root.string() != compare_with_root.root_name().string() + "\\" &&
      compare_with_root.string() != compare_with_root.root_name().string() + "/" && 
      compare_with_root.string() != compare_with_root.root_name().string()) {
      std::string filename(m_inputTextbox);
      bool success = false;

      std::error_code ec;
      if (!filename.empty() && m_type == IFD_DIALOG_SAVE &&
        !ghc::filesystem::exists(m_currentDirectory / filename, ec) && !ghc::filesystem::is_directory(m_currentDirectory / filename, ec))
        success = m_finalize(filename);
      else if (!filename.empty() || m_type == IFD_DIALOG_DIRECTORY)
        success = m_finalize(filename);
      #ifdef _WIN32
      if (!success)
        MessageBeep(MB_ICONERROR);
      #elif (defined(__MACH__) && defined(__APPLE__))
      if (!success)
        NSBeep();
      #else
      (void)success;
      #endif
    }
    if (m_type != IFD_DIALOG_DIRECTORY) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-FLT_MIN);
      int sel = static_cast<int>(m_filterSelection);
      if (ImGui::Combo("##ext_combo", &sel, m_filter.c_str())) {
        m_filterSelection = static_cast<size_t>(sel);
        m_setDirectory(m_currentDirectory, false); // refresh
      }
    }

    // buttons
    float ok_cancel_width = GUI_ELEMENT_SIZE * 7;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ok_cancel_width);
    if (ImGui::Button(m_type == IFD_DIALOG_SAVE ? IFD_SAVE : IFD_OPEN, ImVec2(ok_cancel_width / 2 - ImGui::GetStyle().ItemSpacing.x, 0.0f)) &&
      #if defined(_WIN32)
      ((m_type == IFD_DIALOG_SAVE) || (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(compare_with_root.wstring().c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)) &&
      #endif
      compare_with_root.string() != compare_with_root.root_name().string() + "\\" &&
      compare_with_root.string() != compare_with_root.root_name().string() + "/" &&
      compare_with_root.string() != compare_with_root.root_name().string()) {
      std::string filename(m_inputTextbox);
      bool success = false;

      std::error_code ec;
      ghc::filesystem::path compare_with_root = m_currentDirectory / filename;
      if (!filename.empty() && m_type == IFD_DIALOG_SAVE &&
        !ghc::filesystem::exists(m_currentDirectory / filename, ec) && !ghc::filesystem::is_directory(m_currentDirectory / filename, ec))
        success = m_finalize(filename);
      else if (!filename.empty() || m_type == IFD_DIALOG_DIRECTORY)
        success = m_finalize(filename);
      #ifdef _WIN32
      if (!success)
        MessageBeep(MB_ICONERROR);
      #elif (defined(__MACH__) && defined(__APPLE__))
      if (!success)
        NSBeep();
      #else
      (void)success;
      #endif
    }
    ImGui::SameLine();
    if (ImGui::Button(IFD_CANCEL, ImVec2(-FLT_MIN, 0.0f))) {
      if (m_type == IFD_DIALOG_DIRECTORY)
        m_isOpen = false;
      else {
        m_finalize();
        m_isOpen = false;
      }
    }

    int escapeKey = ImGui::GetIO().KeyMap[ImGuiKey_Escape];
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       escapeKey >= 0 && ImGui::IsKeyPressed(escapeKey))
      m_isOpen = false;
  }
}
const char* ifd::GetDefaultFolderIcon() {
  return (const char*)&folder_icon[0];
}
const char* ifd::GetDefaultFileIcon() {
  return (const char*)&file_icon[0];
}
