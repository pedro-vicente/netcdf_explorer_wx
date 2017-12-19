//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file 
#include "wx/wx.h"
#include "wx/splitter.h"
#include "wx/treectrl.h"
#include "wx/artprov.h"
#include "wx/imaglist.h"
#include "wx/grid.h"
#include "wx/mdi.h"
#include "wx/laywin.h"
#include "wx/filename.h"
#include "wx/filehistory.h"
#include "wx/config.h"
#include "wx/toolbar.h"
#include "wx/cmdline.h"
#include "icons/sample.xpm"
#include "icons/back.xpm"
#include "icons/forward.xpm"
#include "icons/doc_blue.xpm"
#include <algorithm>
#include <vector>
#include "netcdf.h"


//OPeNDAP
//http://www.esrl.noaa.gov/psd/thredds/dodsC/Datasets/cmap/enh/precip.mon.mean.nc

//Widget IDs
enum
{
  ID_FRAME_OPENDAP = wxID_HIGHEST + 1,
  ID_WINDOW_SASH,
  ID_TREE_LOAD_ITEM,
  ID_TREE_DIMENSIONS,
  ID_DIMENSIONS_ROWS,
  ID_DIMENSIONS_COLS,
  ID_DIMENSIONS_LAYERS,
  ID_CHILD_QUIT
};

//Widget IDs for layer navigation 
//reserve enumerators for possible maximum of 32 dimensions
const int max_dimension = 32;
enum
{
  ID_CHILD_FORWARD = wxID_HIGHEST + 1001,
  ID_CHILD_BACK = ID_CHILD_FORWARD + max_dimension,
  ID_CHILD_INDEX_LAYER = ID_CHILD_BACK + max_dimension
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ncdim_t
//a netCDF dimension has a name and a size
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ncdim_t
{
public:
  ncdim_t(const char* name, size_t size) :
    m_name(name),
    m_size(size)
  {
  }
  wxString m_name;
  size_t m_size;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ncvar_t
//a netCDF variable has a name, a netCDF type, data buffer, and an array of dimensions
//defined in iteration
//data buffer is stored on per load variable from tree using netCDF API from item input
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ncvar_t
{
public:
  ncvar_t(const char* name, nc_type nc_typ, const std::vector<ncdim_t> &ncdim) :
    m_name(name),
    m_nc_type(nc_typ),
    m_ncdim(ncdim)
  {
    m_buf = NULL;
  }
  ~ncvar_t()
  {
    switch (m_nc_type)
    {
    case NC_STRING:
      if (m_buf)
      {
        char **buf_string = NULL;
        size_t idx_buf = 0;
        buf_string = static_cast<char**> (m_buf);
        if (m_ncdim.size())
        {
          for (size_t idx_dmn = 0; idx_dmn < m_ncdim.size(); idx_dmn++)
          {
            for (size_t idx_sz = 0; idx_sz < m_ncdim[idx_dmn].m_size; idx_sz++)
            {
              free(buf_string[idx_buf]);
              idx_buf++;
            }
          }
        }
        else
        {
          free(*buf_string);
        }
        free(static_cast<char**>(buf_string));
      }
      break;
    default:
      free(m_buf);
    }
  }
  void store(void *buf)
  {
    m_buf = buf;
  }
  wxString m_name;
  nc_type m_nc_type;
  void *m_buf;
  std::vector<ncdim_t> m_ncdim;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//grid_policy_t
/////////////////////////////////////////////////////////////////////////////////////////////////////

class grid_policy_t
{
public:
  grid_policy_t(const std::vector<ncdim_t> &ncdim)
  {
    //define a grid policy
    if (ncdim.size() == 0)
    {
      m_dim_rows = -1;
      m_dim_cols = -1;
    }
    else if (ncdim.size() == 1)
    {
      m_dim_rows = 0;
      m_dim_cols = -1;
    }
    else if (ncdim.size() == 2)
    {
      m_dim_rows = 0;
      m_dim_cols = 1;
    }
    else if (ncdim.size() >= 3)
    {
      m_dim_cols = ncdim.size() - 1; //2 for 3D
      m_dim_rows = ncdim.size() - 2; //1 for 3D
      for (size_t idx_dmn = 0; idx_dmn < ncdim.size() - 2; idx_dmn++)
      {
        m_dim_layers.push_back(idx_dmn); //0 for 3D
      }
    }
  }
  int m_dim_rows;   // choose dimension to be displayed by rows 
  int m_dim_cols;   // choose dimension to be displayed by columns 
  std::vector<int> m_dim_layers; // choose dimensions to be displayed by layers 
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//GetAppName
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxString GetAppName()
{
  wxString str(wxT("netCDF Explorer"));
  return str;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//GetPathComponent
//return last component of POSIX path name
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxString GetPathComponent(const wxString &path)
{
  wxString name;
  bool isurl = (path.SubString(0, 3) == "http");
  if (isurl)
  {
    return path;
  }
  else
  {
#ifdef __WINDOWS__
    name = path.AfterLast('\\');
#else
    name = path.AfterLast('/');
#endif
  }
  return name;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxItemData
/////////////////////////////////////////////////////////////////////////////////////////////////////

class wxItemData : public wxTreeItemData
{
public:
  enum ItemKind
  {
    Root,
    Group,
    Variable,
    Attribute
  };

  wxItemData(ItemKind kind, const wxString& file_name, const wxString& grp_nm_fll, const wxString& item_nm, wxItemData *item_data_prn,
    ncvar_t *ncvar, grid_policy_t *grid_policy) :
    m_file_name(file_name),
    m_grp_nm_fll(grp_nm_fll),
    m_item_nm(item_nm),
    m_kind(kind),
    m_item_data_prn(item_data_prn),
    m_ncvar(ncvar),
    m_grid_policy(grid_policy)
  {
  }
  ~wxItemData()
  {
    delete m_ncvar;
    for (size_t idx_dmn = 0; idx_dmn < m_ncvar_crd.size(); idx_dmn++)
    {
      delete m_ncvar_crd[idx_dmn];
    }
    delete m_grid_policy;
  }
  wxString m_file_name;  // (Root/Variable/Group/Attribute) file name
  wxString m_grp_nm_fll; // (Group) full name of group
  wxString m_item_nm; // (Root/Variable/Group/Attribute ) item name to display on tree
  ItemKind m_kind; // (Root/Variable/Group/Attribute) type of item 
  std::vector<wxString> m_var_nms; // (Group) list of variables if item is group (filled in file iteration)
  wxItemData *m_item_data_prn; //  (Variable/Group) item data of the parent group (to get list of variables in group)
  ncvar_t *m_ncvar; // (Variable) netCDF variable to display
  std::vector<ncvar_t *> m_ncvar_crd; // (Variable) optional coordinate variables for variable
  grid_policy_t *m_grid_policy; // (Variable) current grid policy (interactive)
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

class wxTreeCtrlExplorer : public wxTreeCtrl
{
public:
  wxTreeCtrlExplorer(wxWindow *parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style);
  virtual ~wxTreeCtrlExplorer();
  void OnSelChanged(wxTreeEvent& event);
  void OnItemActivated(wxTreeEvent& event);
  void OnItemMenu(wxTreeEvent& event);
  void OnLoadItem(wxCommandEvent& event);
  void OnDimensions(wxCommandEvent& event);
  void OnUpdateDimensions(wxUpdateUIEvent& event);

protected:
  void LoadItem(wxItemData *item_data);
  void ShowVariable(wxItemData *item_data);
  void* LoadVariable(const int nc_id, const int var_id, const nc_type var_type, size_t buf_sz);

private:
  wxDECLARE_EVENT_TABLE();
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

class wxFrameExplorer : public wxMDIParentFrame
{
public:
  wxFrameExplorer();
  ~wxFrameExplorer();
  void OnFileOpen(wxCommandEvent &event);
  void OnFileOpenDap(wxCommandEvent& event);
  void OnQuit(wxCommandEvent& event);
  void OnMRUFile(wxCommandEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnSashDrag(wxSashEvent& event);
  void OnAbout(wxCommandEvent& event);
  int GetSashWidth()
  {
    wxRect rect = m_sash->GetRect();
    return rect.GetWidth();
  };
  int OpenFile(const wxString& file_name);

protected:
  int Iterate(const wxString& file_name, const int grp_id, wxTreeItemId item_id);
  wxTreeCtrlExplorer *m_tree;
  wxSashLayoutWindow *m_sash;
  wxTreeItemId m_tree_root;
  wxFileHistory m_file_history;

  //tree icons
  enum
  {
    id_folder,
    id_variable,
    id_attribute
  };

private:
  wxDECLARE_EVENT_TABLE();
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxAppExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

class wxAppExplorer : public wxApp
{
public:
  virtual bool OnInit();
  virtual void OnInitCmdLine(wxCmdLineParser& parser);
  virtual bool OnCmdLineParsed(wxCmdLineParser& parser);

protected:
  wxString m_file_name;
};

DECLARE_APP(wxAppExplorer)
IMPLEMENT_APP(wxAppExplorer)

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxAppExplorer::OnInit
/////////////////////////////////////////////////////////////////////////////////////////////////////

bool wxAppExplorer::OnInit()
{
  if (!wxApp::OnInit())
    return false;

  wxFrameExplorer *frame = new wxFrameExplorer();
  if (!m_file_name.empty())
  {
    frame->OpenFile(m_file_name);
  }
  frame->Show(true);
  frame->Maximize();
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxAppExplorer::OnInitCmdLine
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxAppExplorer::OnInitCmdLine(wxCmdLineParser& parser)
{
  wxApp::OnInitCmdLine(parser);
  parser.AddParam("input file", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxAppExplorer::OnCmdLineParsed
/////////////////////////////////////////////////////////////////////////////////////////////////////

bool wxAppExplorer::OnCmdLineParsed(wxCmdLineParser& parser)
{
  if (!wxApp::OnCmdLineParsed(parser))
    return false;

  if (parser.GetParamCount())
  {
    m_file_name = parser.GetParam(0);
  }

  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxGridLayers
/////////////////////////////////////////////////////////////////////////////////////////////////////

class wxGridLayers : public wxGrid
{
public:
  wxGridLayers(wxWindow *parent, const wxSize& size, wxItemData *item_data);
  ~wxGridLayers();
  void ShowGrid();

public:
  std::vector<int> m_layer;  // current selected layer of a dimension > 2 
  wxItemData *m_item_data; // the tree item that generated this grid (convenience pointer to data in wxItemData)
  ncvar_t *m_ncvar; // netCDF variable to display (convenience pointer to data in wxItemData)
  wxString GetFormat(const nc_type typ);

protected:
  int m_nbr_rows;   // number of rows
  int m_nbr_cols;   // number of columns
  int m_dim_rows;   // choose rows (convenience duplicate to data in wxItemData)
  int m_dim_cols;   // choose columns (convenience duplicate to data in wxItemData)
  std::vector<ncvar_t *> m_ncvar_crd; // optional coordinate variables for variable (convenience duplicate to data in wxItemData)

private:
  DECLARE_EVENT_TABLE()
};

wxBEGIN_EVENT_TABLE(wxGridLayers, wxGrid)
wxEND_EVENT_TABLE()

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxGridLayers::wxGridLayers
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxGridLayers::wxGridLayers(wxWindow *parent, const wxSize& size, wxItemData *item_data) :
  wxGrid(parent, wxID_ANY, wxPoint(0, 0), size, wxNO_BORDER),
  m_item_data(item_data),
  m_ncvar(item_data->m_ncvar),
  m_dim_rows(item_data->m_grid_policy->m_dim_rows),
  m_dim_cols(item_data->m_grid_policy->m_dim_cols),
  m_ncvar_crd(item_data->m_ncvar_crd)
{
  //currently selected layers for dimensions greater than two are the first layer
  if (m_ncvar->m_ncdim.size() > 2)
  {
    for (size_t idx_dmn = 0; idx_dmn < m_ncvar->m_ncdim.size() - 2; idx_dmn++)
    {
      m_layer.push_back(0);
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //define grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if (m_ncvar->m_ncdim.size() == 0)
  {
    m_nbr_rows = 1;
    m_nbr_cols = 1;
  }
  else if (m_ncvar->m_ncdim.size() == 1)
  {
    assert(m_dim_rows == 0);
    m_nbr_rows = m_ncvar->m_ncdim[m_dim_rows].m_size;
    m_nbr_cols = 1;
  }
  else
  {
    m_nbr_rows = m_ncvar->m_ncdim[m_dim_rows].m_size;
    m_nbr_cols = m_ncvar->m_ncdim[m_dim_cols].m_size;
  }

  this->CreateGrid(m_nbr_rows, m_nbr_cols);

  //show data
  this->ShowGrid();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxGridLayers::~wxGridLayers
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxGridLayers::~wxGridLayers()
{

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild
//version 3.0 does not have toolbars for wxMDIChildFrame
/////////////////////////////////////////////////////////////////////////////////////////////////////

class wxFrameChild : public wxFrame
{
public:
  wxFrameChild(wxMDIParentFrame *parent, const wxString& title, wxItemData *item_data);
  void OnQuit(wxCommandEvent& event);
  void OnActivate(wxActivateEvent& event);
  void OnForward(wxCommandEvent& event);
  void OnBack(wxCommandEvent& event);
  void OnChoiceLayer(wxCommandEvent &event);

protected:
  wxGridLayers *m_grid;
  void InitToolBar(wxToolBar* tb, wxItemData *item_data);

private:
  DECLARE_EVENT_TABLE()
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::wxFrameChild
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(wxFrameChild, wxFrame)
EVT_MENU(ID_CHILD_QUIT, wxFrameChild::OnQuit)
EVT_TOOL(ID_CHILD_FORWARD, wxFrameChild::OnForward)
EVT_TOOL(ID_CHILD_FORWARD + 1, wxFrameChild::OnForward)
EVT_TOOL(ID_CHILD_FORWARD + 2, wxFrameChild::OnForward)
EVT_TOOL(ID_CHILD_BACK, wxFrameChild::OnBack)
EVT_TOOL(ID_CHILD_BACK + 1, wxFrameChild::OnBack)
EVT_TOOL(ID_CHILD_BACK + 2, wxFrameChild::OnBack)
EVT_CHOICE(ID_CHILD_INDEX_LAYER, wxFrameChild::OnChoiceLayer)
EVT_CHOICE(ID_CHILD_INDEX_LAYER + 1, wxFrameChild::OnChoiceLayer)
EVT_CHOICE(ID_CHILD_INDEX_LAYER + 2, wxFrameChild::OnChoiceLayer)
wxEND_EVENT_TABLE()

wxFrameChild::wxFrameChild(wxMDIParentFrame *parent, const wxString& title, wxItemData *item_data) :
  wxFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
    wxDEFAULT_FRAME_STYLE | wxNO_FULL_REPAINT_ON_RESIZE | wxFRAME_FLOAT_ON_PARENT)
{
  SetIcon(wxICON(sample));
  m_grid = new wxGridLayers(this, GetClientSize(), item_data);
  //3D variable, add a layer navigation toolbar with extra dimensions above rows and columns
  if (item_data->m_ncvar->m_ncdim.size() >= 3)
  {
    CreateToolBar(wxNO_BORDER | wxTB_FLAT | wxTB_HORIZONTAL, wxID_ANY, "layer");
    InitToolBar(GetToolBar(), item_data);
  }
  Raise();
  //nicely rearranje children under parent frame
  wxPoint pos = GetPosition();
  wxFrameExplorer *frame = (wxFrameExplorer*)GetParent();
  pos.x += frame->GetSashWidth();
  SetPosition(pos);

  //set size accordingly to grid and/or toolbar
  wxSize size(std::max(GetToolBar() ? static_cast<int> ((m_grid->m_layer.size() * (GetToolBar()->GetToolSize().GetWidth() * 2 + 130))) : 0,
    (m_grid->GetNumberCols() + 2) * m_grid->GetDefaultColSize()),
    (m_grid->GetNumberRows() + 2) * m_grid->GetDefaultRowSize());
  this->SetClientSize(std::max(GetClientSize().GetWidth(), size.GetWidth()), std::max(GetClientSize().GetHeight(), size.GetHeight()));
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::InitToolBar
//add dimension choices for variables with rank greater than 2
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameChild::InitToolBar(wxToolBar* tb, wxItemData *item_data)
{
  float *buf_float = NULL;
  double *buf_double = NULL;
  int *buf_int = NULL;
  short *buf_short = NULL;
  signed char *buf_byte = NULL;
  unsigned char *buf_ubyte = NULL;
  unsigned short *buf_ushort = NULL;
  unsigned int *buf_uint = NULL;
  long long *buf_int64 = NULL;
  unsigned long long *buf_uint64 = NULL;

#if defined (__WXMSW__)
  tb->SetToolBitmapSize(tb->GetToolBitmapSize() + wxSize(0, 10));
#endif

  //number of dimensions above a two-dimensional dataset
  for (size_t idx_dmn = 0; idx_dmn < m_grid->m_layer.size(); idx_dmn++)
  {
    tb->AddTool(ID_CHILD_FORWARD + idx_dmn, wxT("Forward"), wxBitmap(forward_xpm), wxT("Move forward to next layer."));
    tb->AddTool(ID_CHILD_BACK + idx_dmn, wxT("Back"), wxBitmap(back_xpm), wxT("Return to previous layer."));
    wxArrayString vec_str;

    //coordinate variable exists
    if (item_data->m_ncvar_crd[idx_dmn] != NULL)
    {
      void *buf = item_data->m_ncvar_crd[idx_dmn]->m_buf;
      size_t size = item_data->m_ncvar->m_ncdim[idx_dmn].m_size;
      switch (item_data->m_ncvar_crd[idx_dmn]->m_nc_type)
      {
      case NC_FLOAT:
        buf_float = static_cast<float*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_FLOAT), buf_float[idx]));
        }
        break;
      case NC_DOUBLE:
        buf_double = static_cast<double*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_DOUBLE), buf_double[idx]));
        }
        break;
      case NC_INT:
        buf_int = static_cast<int*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_INT), buf_int[idx]));
        }
        break;
      case NC_SHORT:
        buf_short = static_cast<short*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_SHORT), buf_short[idx]));
        }
        break;
      case NC_BYTE:
        buf_byte = static_cast<signed char*>  (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_BYTE), buf_byte[idx]));
        }
        break;
      case NC_UBYTE:
        buf_ubyte = static_cast<unsigned char*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_UBYTE), buf_ubyte[idx]));
        }
        break;
      case NC_USHORT:
        buf_ushort = static_cast<unsigned short*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_USHORT), buf_ushort[idx]));
        }
        break;
      case NC_UINT:
        buf_uint = static_cast<unsigned int*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_UINT), buf_uint[idx]));
        }
        break;
      case NC_INT64:
        buf_int64 = static_cast<long long*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_INT64), buf_int64[idx]));
        }
        break;
      case NC_UINT64:
        buf_uint64 = static_cast<unsigned long long*> (buf);
        for (size_t idx = 0; idx < size; idx++)
        {
          vec_str.Add(wxString::Format(m_grid->GetFormat(NC_UINT64), buf_uint64[idx]));
        }
        break;
      } //switch
    }
    else
    {
      for (unsigned int idx = 0; idx < item_data->m_ncvar->m_ncdim[idx_dmn].m_size; idx++)
      {
        vec_str.Add(wxString::Format("%u", idx + 1));
      }
    }

    wxChoice *choice_layer = new wxChoice(tb, ID_CHILD_INDEX_LAYER + idx_dmn, wxDefaultPosition, wxSize(100, 30), vec_str);
    //select first index
    choice_layer->SetSelection(m_grid->m_layer[idx_dmn]);
    assert(m_grid->m_layer[idx_dmn] == 0);
    tb->AddControl(choice_layer);
  }
  tb->Realize();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::OnQuit
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameChild::OnQuit(wxCommandEvent& WXUNUSED(event))
{
  Close(true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::OnChoiceLayer
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameChild::OnChoiceLayer(wxCommandEvent& event)
{
  //find selected choice control
  for (size_t idx_dmn = 0; idx_dmn < m_grid->m_layer.size(); idx_dmn++)
  {
    if (event.GetId() == static_cast<int>(ID_CHILD_INDEX_LAYER + idx_dmn))
    {
      wxChoice* choice_layer = (wxChoice*)GetToolBar()->FindControl(ID_CHILD_INDEX_LAYER + idx_dmn);
      m_grid->m_layer[idx_dmn] = choice_layer->GetSelection();
      m_grid->ShowGrid();
      m_grid->Refresh();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::OnForward
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameChild::OnForward(wxCommandEvent& event)
{
  //find selected tool
  for (size_t idx_dmn = 0; idx_dmn < m_grid->m_layer.size(); idx_dmn++)
  {
    if (event.GetId() == static_cast<int>(ID_CHILD_FORWARD + idx_dmn))
    {
      m_grid->m_layer[idx_dmn]++;
      if ((size_t)m_grid->m_layer[idx_dmn] >= m_grid->m_ncvar->m_ncdim[idx_dmn].m_size)
      {
        m_grid->m_layer[idx_dmn] = m_grid->m_ncvar->m_ncdim[idx_dmn].m_size - 1;
        return;
      }
      //update choice
      wxChoice* choice_layer = (wxChoice*)GetToolBar()->FindControl(ID_CHILD_INDEX_LAYER + idx_dmn);
      choice_layer->SetSelection(m_grid->m_layer[idx_dmn]);
      m_grid->ShowGrid();
      m_grid->Refresh();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::OnBack
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameChild::OnBack(wxCommandEvent& event)
{
  //find selected tool
  for (size_t idx_dmn = 0; idx_dmn < m_grid->m_layer.size(); idx_dmn++)
  {
    if (event.GetId() == static_cast<int>(ID_CHILD_BACK + idx_dmn))
    {
      m_grid->m_layer[idx_dmn]--;
      if (m_grid->m_layer[idx_dmn] < 0)
      {
        m_grid->m_layer[idx_dmn] = 0;
        return;
      }
      //update choice
      wxChoice* choice_layer = (wxChoice*)GetToolBar()->FindControl(ID_CHILD_INDEX_LAYER + idx_dmn);
      choice_layer->SetSelection(m_grid->m_layer[idx_dmn]);
      m_grid->ShowGrid();
      m_grid->Refresh();
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameChild::OnActivate
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameChild::OnActivate(wxActivateEvent& event)
{
  if (event.GetActive() && m_grid)
    m_grid->SetFocus();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::wxFrameExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(wxFrameExplorer, wxMDIParentFrame)
EVT_MENU(wxID_OPEN, wxFrameExplorer::OnFileOpen)
EVT_MENU(ID_FRAME_OPENDAP, wxFrameExplorer::OnFileOpenDap)
EVT_MENU(wxID_EXIT, wxFrameExplorer::OnQuit)
EVT_MENU_RANGE(wxID_FILE1, wxID_FILE9, wxFrameExplorer::OnMRUFile)
EVT_SIZE(wxFrameExplorer::OnSize)
EVT_SASH_DRAGGED_RANGE(ID_WINDOW_SASH, ID_WINDOW_SASH, wxFrameExplorer::OnSashDrag)
EVT_MENU(wxID_ABOUT, wxFrameExplorer::OnAbout)
wxEND_EVENT_TABLE()

wxFrameExplorer::wxFrameExplorer() : wxMDIParentFrame(NULL, wxID_ANY, GetAppName(), wxDefaultPosition, wxSize(550, 840))
{
  int w, h;
  GetClientSize(&w, &h);

  SetIcon(wxICON(sample));
  wxMenu *menu_file = new wxMenu;
  menu_file->Append(wxID_OPEN, _("&Open...\tCtrl+O"));
  menu_file->Append(ID_FRAME_OPENDAP, wxT("OPeN&DAP...\tCtrl+D"));
  menu_file->AppendSeparator();
  menu_file->Append(wxID_EXIT, "E&xit\tAlt-X", "Quit");
  wxMenu *menu_help = new wxMenu;
  menu_help->Append(wxID_ABOUT, "&About\tF1", "Show about dialog");
  wxMenuBar *menu_bar = new wxMenuBar();
  menu_bar->Append(menu_file, "&File");
  menu_bar->Append(menu_help, "&Help");
  SetMenuBar(menu_bar);
  m_file_history.UseMenu(menu_file);
  m_file_history.AddFilesToMenu(menu_file);
  m_file_history.Load(*wxConfig::Get());
  CreateStatusBar(1);

  wxSashLayoutWindow* win;
  win = new wxSashLayoutWindow(this, ID_WINDOW_SASH, wxDefaultPosition, wxDefaultSize, wxNO_BORDER | wxSW_3D | wxCLIP_CHILDREN);
  win->SetDefaultSize(wxSize(300, h));
  win->SetOrientation(wxLAYOUT_VERTICAL);
  win->SetAlignment(wxLAYOUT_LEFT);
  win->SetSashVisible(wxSASH_RIGHT, true);
  win->SetExtraBorderSize(10);
  win->SetMinimumSizeX(100);
  win->SetBackgroundColour(*wxWHITE);
  m_sash = win;

  m_tree = new wxTreeCtrlExplorer(m_sash, wxID_ANY, wxPoint(0, 0), wxSize(160, 250), wxTR_DEFAULT_STYLE | wxNO_BORDER | wxTR_HIDE_ROOT);
  wxImageList* imglist = new wxImageList(16, 16, true, 2);
  wxBitmap bitmaps[3];
  bitmaps[id_folder] = wxBitmap(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16)));
  bitmaps[id_variable] = wxBitmap(wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16)));
  bitmaps[id_attribute] = wxBitmap(doc_blue_xpm);
  imglist->Add(bitmaps[id_folder]);
  imglist->Add(bitmaps[id_variable]);
  imglist->Add(bitmaps[id_attribute]);
  m_tree->AssignImageList(imglist);
  m_tree_root = m_tree->AddRoot("");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::~wxFrameExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxFrameExplorer::~wxFrameExplorer()
{
  m_file_history.Save(*wxConfig::Get());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnQuit
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnQuit(wxCommandEvent& WXUNUSED(event))
{
  m_file_history.Save(*wxConfig::Get());
  Close(true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnMRUFile
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnMRUFile(wxCommandEvent& event)
{
  wxString path(m_file_history.GetHistoryFile(event.GetId() - wxID_FILE1));
  if (!path.empty())
  {
    OpenFile(path);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnFileOpen
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnFileOpen(wxCommandEvent &WXUNUSED(event))
{
  wxString path;
  wxFileDialog dlg(this, wxT("Open file"),
    wxEmptyString,
    wxEmptyString,
    wxString::Format
    (
      wxT("netCDF (*.nc)|*.nc|All files (%s)|%s"),
      wxFileSelectorDefaultWildcardStr,
      wxFileSelectorDefaultWildcardStr
    ),
    wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_CHANGE_DIR);
  if (dlg.ShowModal() != wxID_OK) return;
  path = dlg.GetPath();
  if (this->OpenFile(path) != NC_NOERR)
  {

  }
  m_file_history.AddFileToHistory(path);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnFileOpenDap
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnFileOpenDap(wxCommandEvent& WXUNUSED(event))
{
  wxTextEntryDialog dialog(this, wxT("OPeNDAP URL:"), wxT("OPeNDAP"));
  if (dialog.ShowModal() == wxID_OK)
  {
    wxString path = dialog.GetValue();
    if (this->OpenFile(path) != NC_NOERR)
    {

    }
    m_file_history.AddFileToHistory(path);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnSashDrag
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnSashDrag(wxSashEvent& event)
{
  int w, h;
  GetClientSize(&w, &h);

  if (event.GetDragStatus() == wxSASH_STATUS_OUT_OF_RANGE)
    return;

  switch (event.GetId())
  {
  case ID_WINDOW_SASH:
    m_sash->SetDefaultSize(wxSize(event.GetDragRect().width, h));
    break;
  }

  wxLayoutAlgorithm layout;
  layout.LayoutMDIFrame(this);
  GetClientWindow()->Refresh();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnSize
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnSize(wxSizeEvent& WXUNUSED(event))
{
  wxLayoutAlgorithm layout;
  layout.LayoutMDIFrame(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OpenFile
/////////////////////////////////////////////////////////////////////////////////////////////////////

int wxFrameExplorer::OpenFile(const wxString& file_name)
{
  int nc_id;

  if (nc_open(file_name, NC_NOWRITE, &nc_id) != NC_NOERR)
  {
    return -1;
  }

  //root item
  wxItemData *item_data = new wxItemData(wxItemData::Root,
    file_name,
    file_name,
    wxString("/"),
    (wxItemData*)NULL,
    (ncvar_t*)NULL,
    (grid_policy_t*)NULL);

  //last component of full path file name used for root tree only
  wxTreeItemId root = m_tree->AppendItem(m_tree_root, GetPathComponent(file_name), 0, 0, item_data);

  if (Iterate(file_name, nc_id, root) != NC_NOERR)
  {

  }

  if (nc_close(nc_id) != NC_NOERR)
  {

  }

  return NC_NOERR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::Iterate
/////////////////////////////////////////////////////////////////////////////////////////////////////

int wxFrameExplorer::Iterate(const wxString& file_name, const int grp_id, wxTreeItemId item_id)
{
  char grp_nm[NC_MAX_NAME + 1]; // group name 
  char var_nm[NC_MAX_NAME + 1]; // variable name 
  char *grp_nm_fll = NULL; // group full name 
  int nbr_att; // number of attributes 
  int nbr_dmn_grp; // number of dimensions for group 
  int nbr_var; // number of variables 
  int nbr_grp; // number of sub-groups in this group 
  int nbr_dmn_var; // number of dimensions for variable 
  nc_type var_typ; // netCDF type 
  int *grp_ids; // sub-group IDs array
  size_t grp_nm_lng; //lenght of full group name
  int var_dimid[NC_MAX_VAR_DIMS]; // dimensions for variable
  size_t dmn_sz[NC_MAX_VAR_DIMS]; // dimensions for variable sizes
  char dmn_nm_var[NC_MAX_NAME + 1]; //dimension name

  //get item data (of parent item), to store a list of variable names 
  wxItemData *item_data_prn = (wxItemData *)m_tree->GetItemData(item_id);
  assert(item_data_prn->m_kind == wxItemData::Group || item_data_prn->m_kind == wxItemData::Root);

  // get full name of (parent) group
  if (nc_inq_grpname_full(grp_id, &grp_nm_lng, NULL) != NC_NOERR)
  {

  }

  grp_nm_fll = new char[grp_nm_lng + 1];

  if (nc_inq_grpname_full(grp_id, &grp_nm_lng, grp_nm_fll) != NC_NOERR)
  {

  }

  if (nc_inq(grp_id, &nbr_dmn_grp, &nbr_var, &nbr_att, (int *)NULL) != NC_NOERR)
  {

  }

  for (int idx_var = 0; idx_var < nbr_var; idx_var++)
  {
    std::vector<ncdim_t> ncdim; //dimensions for each variable 

    if (nc_inq_var(grp_id, idx_var, var_nm, &var_typ, &nbr_dmn_var, var_dimid, &nbr_att) != NC_NOERR)
    {

    }

    //store variable name in parent group item (for coordinate variables detection)
    item_data_prn->m_var_nms.push_back(var_nm);

    //get dimensions
    for (int idx_dmn = 0; idx_dmn < nbr_dmn_var; idx_dmn++)
    {
      //dimensions belong to groups
      if (nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
      {

      }

      //store dimension 
      ncdim_t dim(dmn_nm_var, dmn_sz[idx_dmn]);
      ncdim.push_back(dim);
    }

    //store a ncvar_t
    ncvar_t *ncvar = new ncvar_t(var_nm, var_typ, ncdim);

    //define a grid dimensions policy
    grid_policy_t *grid_policy = new grid_policy_t(ncdim);

    //append item
    wxItemData *item_data_var = new wxItemData(wxItemData::Variable,
      file_name,
      grp_nm_fll,
      var_nm,
      item_data_prn,
      ncvar,
      grid_policy);
    m_tree->AppendItem(item_id, var_nm, 1, 1, item_data_var);
  }

  if (nc_inq_grps(grp_id, &nbr_grp, (int *)NULL) != NC_NOERR)
  {

  }

  grp_ids = new int[nbr_grp];

  if (nc_inq_grps(grp_id, &nbr_grp, grp_ids) != NC_NOERR)
  {

  }

  for (int idx_grp = 0; idx_grp < nbr_grp; idx_grp++)
  {
    if (nc_inq_grpname(grp_ids[idx_grp], grp_nm) != NC_NOERR)
    {

    }

    //group item
    wxItemData *item_data_grp = new wxItemData(wxItemData::Group,
      file_name,
      grp_nm_fll,
      grp_nm,
      item_data_prn,
      (ncvar_t*)NULL,
      (grid_policy_t*)NULL);
    wxTreeItemId item_id_grp = m_tree->AppendItem(item_id, grp_nm, 0, 0, item_data_grp);

    if (Iterate(file_name, grp_ids[idx_grp], item_id_grp) != NC_NOERR)
    {

    }
  }

  delete[] grp_ids;
  delete[] grp_nm_fll;

  return NC_NOERR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxFrameExplorer::OnAbout
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxFrameExplorer::OnAbout(wxCommandEvent& WXUNUSED(event))
{
  wxMessageBox("(c) 2015 Pedro Vicente -- Space Research Software LLC\n\n", GetAppName(), wxOK, this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::wxTreeCtrlExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(wxTreeCtrlExplorer, wxTreeCtrl)
EVT_TREE_SEL_CHANGED(wxID_ANY, wxTreeCtrlExplorer::OnSelChanged)
EVT_TREE_ITEM_ACTIVATED(wxID_ANY, wxTreeCtrlExplorer::OnItemActivated)
EVT_TREE_ITEM_MENU(wxID_ANY, wxTreeCtrlExplorer::OnItemMenu)
EVT_MENU(ID_TREE_LOAD_ITEM, wxTreeCtrlExplorer::OnLoadItem)
wxEND_EVENT_TABLE()

wxTreeCtrlExplorer::wxTreeCtrlExplorer(wxWindow *parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
  : wxTreeCtrl(parent, id, pos, size, style)
{

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::~wxTreeCtrlExplorer
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxTreeCtrlExplorer::~wxTreeCtrlExplorer()
{

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::ShowVariable
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTreeCtrlExplorer::ShowVariable(wxItemData *item_data)
{
  //if not loaded, read buffer from file 
  if (item_data->m_ncvar->m_buf == NULL)
  {
    LoadItem(item_data);
  }

  //show in grid
  wxSashLayoutWindow *sash = (wxSashLayoutWindow*)GetParent();
  wxFrameExplorer *frame = (wxFrameExplorer*)sash->GetParent();
  wxFrameChild *subframe = new wxFrameChild(frame,
    wxString::Format(wxT("%s : %s"), GetPathComponent(item_data->m_file_name), item_data->m_item_nm),
    item_data);
  subframe->Show(true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::OnSelChanged
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTreeCtrlExplorer::OnSelChanged(wxTreeEvent& event)
{
  event.Skip();
#ifdef _DEBUG
  wxTreeItemId item_id = event.GetItem();
  wxItemData *item_data = (wxItemData *)GetItemData(item_id);
  if (item_data->m_kind != wxItemData::Variable)
  {
    return;
  }
  ShowVariable(item_data);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::OnItemActivated
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTreeCtrlExplorer::OnItemActivated(wxTreeEvent& event)
{
  wxTreeItemId item_id = event.GetItem();
  wxItemData *item_data = (wxItemData *)GetItemData(item_id);
  event.Skip();
  if (item_data->m_kind != wxItemData::Variable)
  {
    return;
  }
  ShowVariable(item_data);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::LoadItem
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTreeCtrlExplorer::LoadItem(wxItemData *item_data)
{
  char var_nm[NC_MAX_NAME + 1]; // variable name 
  char dmn_nm_var[NC_MAX_NAME + 1]; //dimension name
  int nc_id;
  int grp_id;
  int var_id;
  nc_type var_type;
  int nbr_dmn;
  int var_dimid[NC_MAX_VAR_DIMS];
  size_t dmn_sz[NC_MAX_VAR_DIMS];
  size_t buf_sz; // variable size
  int fl_fmt;

  assert(item_data->m_kind == wxItemData::Variable);

  if (nc_open(item_data->m_file_name, NC_NOWRITE, &nc_id) != NC_NOERR)
  {

  }

  //need a file format inquiry, since nc_inq_grp_full_ncid does not handle netCDF3 cases
  if (nc_inq_format(nc_id, &fl_fmt) != NC_NOERR)
  {

  }

  if (fl_fmt == NC_FORMAT_NETCDF4 || fl_fmt == NC_FORMAT_NETCDF4_CLASSIC)
  {
    // obtain group ID for netCDF4 files
    if (nc_inq_grp_full_ncid(nc_id, item_data->m_grp_nm_fll, &grp_id) != NC_NOERR)
    {

    }
  }
  else
  {
    //make the group ID the file ID for netCDF3 cases
    grp_id = nc_id;
  }

  //all hunky dory from here 

  // get variable ID
  if (nc_inq_varid(grp_id, item_data->m_item_nm, &var_id) != NC_NOERR)
  {

  }

  if (nc_inq_var(grp_id, var_id, var_nm, &var_type, &nbr_dmn, var_dimid, (int *)NULL) != NC_NOERR)
  {

  }

  //detect coordinate variables 
  for (int idx_dmn = 0; idx_dmn < nbr_dmn; idx_dmn++)
  {
    int has_crd_var = 0;

    //dimensions belong to groups
    if (nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
    {

    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    //look up possible coordinate variables
    //traverse all variables in group and match a variable name 
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    for (size_t idx_var = 0; idx_var < item_data->m_item_data_prn->m_var_nms.size(); idx_var++)
    {
      wxString str_var_nm(item_data->m_item_data_prn->m_var_nms[idx_var]);

      if (str_var_nm == wxString(dmn_nm_var))
      {
        has_crd_var = 1;
        break;
      }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    //a coordinate variable was found
    //since the lookup was only in the same group, get the variable information on this group 
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    if (has_crd_var)
    {
      int crd_var_id;
      char crd_var_nm[NC_MAX_NAME + 1];
      int crd_nbr_dmn;
      int crd_var_dimid[NC_MAX_VAR_DIMS];
      size_t crd_dmn_sz[NC_MAX_VAR_DIMS];
      nc_type crd_var_type = NC_NAT;

      // get coordinate variable ID (using the dimension name, since there was a match to a variable)
      if (nc_inq_varid(grp_id, dmn_nm_var, &crd_var_id) != NC_NOERR)
      {

      }

      if (nc_inq_var(grp_id, crd_var_id, crd_var_nm, &crd_var_type, &crd_nbr_dmn, crd_var_dimid, (int *)NULL) != NC_NOERR)
      {

      }

      assert(wxString(crd_var_nm) == wxString(dmn_nm_var));

      if (crd_nbr_dmn == 1)
      {
        //get size
        if (nc_inq_dim(grp_id, crd_var_dimid[0], (char *)NULL, &crd_dmn_sz[0]) != NC_NOERR)
        {

        }

        //store dimension 
        std::vector<ncdim_t> ncdim; //dimensions for each variable 
        ncdim_t dim(dmn_nm_var, crd_dmn_sz[0]);
        ncdim.push_back(dim);

        //store a ncvar_t
        ncvar_t *ncvar = new ncvar_t(crd_var_nm, crd_var_type, ncdim);

        //allocate, load 
        ncvar->store(LoadVariable(grp_id, crd_var_id, crd_var_type, crd_dmn_sz[0]));

        //and store in tree 
        item_data->m_ncvar_crd.push_back(ncvar);
      }
    }
    else
    {
      item_data->m_ncvar_crd.push_back(NULL); //no coordinate variable for this dimension
    }
  }

  //define buffer size
  buf_sz = 1;
  for (int idx_dmn = 0; idx_dmn < nbr_dmn; idx_dmn++)
  {
    buf_sz *= dmn_sz[idx_dmn];
  }

  //allocate buffer and store in item data 
  item_data->m_ncvar->store(LoadVariable(grp_id, var_id, var_type, buf_sz));

  if (nc_close(nc_id) != NC_NOERR)
  {

  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::LoadVariable
/////////////////////////////////////////////////////////////////////////////////////////////////////

void* wxTreeCtrlExplorer::LoadVariable(const int nc_id, const int var_id, const nc_type var_type, size_t buf_sz)
{
  void *buf = NULL;
  switch (var_type)
  {
  case NC_FLOAT:
    buf = malloc(buf_sz * sizeof(float));
    if (nc_get_var_float(nc_id, var_id, static_cast<float *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_DOUBLE:
    buf = malloc(buf_sz * sizeof(double));
    if (nc_get_var_double(nc_id, var_id, static_cast<double *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT:
    buf = malloc(buf_sz * sizeof(int));
    if (nc_get_var_int(nc_id, var_id, static_cast<int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_SHORT:
    buf = malloc(buf_sz * sizeof(short));
    if (nc_get_var_short(nc_id, var_id, static_cast<short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_CHAR:
    buf = malloc(buf_sz * sizeof(char));
    if (nc_get_var_text(nc_id, var_id, static_cast<char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_BYTE:
    buf = malloc(buf_sz * sizeof(signed char));
    if (nc_get_var_schar(nc_id, var_id, static_cast<signed char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UBYTE:
    buf = malloc(buf_sz * sizeof(unsigned char));
    if (nc_get_var_uchar(nc_id, var_id, static_cast<unsigned char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_USHORT:
    buf = malloc(buf_sz * sizeof(unsigned short));
    if (nc_get_var_ushort(nc_id, var_id, static_cast<unsigned short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT:
    buf = malloc(buf_sz * sizeof(unsigned int));
    if (nc_get_var_uint(nc_id, var_id, static_cast<unsigned int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT64:
    buf = malloc(buf_sz * sizeof(long long));
    if (nc_get_var_longlong(nc_id, var_id, static_cast<long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT64:
    buf = malloc(buf_sz * sizeof(unsigned long long));
    if (nc_get_var_ulonglong(nc_id, var_id, static_cast<unsigned long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_STRING:
    buf = malloc(buf_sz * sizeof(char*));
    if (nc_get_var_string(nc_id, var_id, static_cast<char* *>(buf)) != NC_NOERR)
    {
    }
    break;
  }
  return buf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxGridLayers::ShowGrid
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxGridLayers::ShowGrid()
{
  size_t idx_buf = 0;
  //3D
  if (m_layer.size() == 1)
  {
    idx_buf = m_layer[0] * m_nbr_rows * m_nbr_cols;
  }
  //4D
  else if (m_layer.size() == 2)
  {
    idx_buf = m_layer[0] * m_ncvar->m_ncdim[1].m_size + m_layer[1];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  //5D
  else if (m_layer.size() == 3)
  {
    idx_buf = m_layer[0] * m_ncvar->m_ncdim[1].m_size * m_ncvar->m_ncdim[2].m_size
      + m_layer[1] * m_ncvar->m_ncdim[2].m_size
      + m_layer[2];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  float *buf_float = NULL;
  double *buf_double = NULL;
  int *buf_int = NULL;
  short *buf_short = NULL;
  char *buf_char = NULL;
  signed char *buf_byte = NULL;
  unsigned char *buf_ubyte = NULL;
  unsigned short *buf_ushort = NULL;
  unsigned int *buf_uint = NULL;
  long long *buf_int64 = NULL;
  unsigned long long *buf_uint64 = NULL;
  char* *buf_string = NULL;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //labels for columns
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  //columns not defined
  if (m_dim_cols == -1)
  {
    assert(m_nbr_cols == 1);
    this->SetColLabelValue(0, wxString::Format(wxT("%d"), 1));
  }
  else
  {
    //coordinate variable exists
    if (m_ncvar_crd[m_dim_cols] != NULL)
    {
      switch (m_ncvar_crd[m_dim_cols]->m_nc_type)
      {
      case NC_FLOAT:
        buf_float = static_cast<float*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_FLOAT), buf_float[idx_col]));
        }
        break;
      case NC_DOUBLE:
        buf_double = static_cast<double*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_DOUBLE), buf_double[idx_col]));
        }
        break;
      case NC_INT:
        buf_int = static_cast<int*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_INT), buf_int[idx_col]));
        }
        break;
      case NC_SHORT:
        buf_short = static_cast<short*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_SHORT), buf_short[idx_col]));
        }
        break;
      case NC_BYTE:
        buf_byte = static_cast<signed char*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_BYTE), buf_byte[idx_col]));
        }
        break;
      case NC_UBYTE:
        buf_ubyte = static_cast<unsigned char*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_UBYTE), buf_ubyte[idx_col]));
        }
        break;
      case NC_USHORT:
        buf_ushort = static_cast<unsigned short*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_USHORT), buf_ushort[idx_col]));
        }
        break;
      case NC_UINT:
        buf_uint = static_cast<unsigned int*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_UINT), buf_uint[idx_col]));
        }
        break;
      case NC_INT64:
        buf_int64 = static_cast<long long*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_INT64), buf_int64[idx_col]));
        }
        break;
      case NC_UINT64:
        buf_uint64 = static_cast<unsigned long long*> (m_ncvar_crd[m_dim_cols]->m_buf);
        for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
        {
          this->SetColLabelValue(idx_col, wxString::Format(GetFormat(NC_UINT64), buf_uint64[idx_col]));
        }
        break;
      }//switch  
    }
    //coordinate variable does not exist
    else
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetColLabelValue(idx_col, wxString::Format(wxT("%d"), idx_col + 1));
      }
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //labels for rows
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  //rows not defined
  if (m_dim_rows == -1)
  {
    this->SetRowLabelValue(0, wxString::Format(wxT("%d"), 1));
  }
  else
  {
    //coordinate variable exists
    if (m_ncvar_crd[m_dim_rows] != NULL)
    {
      switch (m_ncvar_crd[m_dim_rows]->m_nc_type)
      {
      case NC_FLOAT:
        buf_float = static_cast<float*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_FLOAT), buf_float[idx_row]));
        }
        break;
      case NC_DOUBLE:
        buf_double = static_cast<double*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_DOUBLE), buf_double[idx_row]));
        }
        break;
      case NC_INT:
        buf_int = static_cast<int*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_INT), buf_int[idx_row]));
        }
        break;
      case NC_SHORT:
        buf_short = static_cast<short*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_SHORT), buf_short[idx_row]));
        }
        break;
      case NC_BYTE:
        buf_byte = static_cast<signed char*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_BYTE), buf_byte[idx_row]));
        }
        break;
      case NC_UBYTE:
        buf_ubyte = static_cast<unsigned char*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_UBYTE), buf_ubyte[idx_row]));
        }
        break;
      case NC_USHORT:
        buf_ushort = static_cast<unsigned short*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_USHORT), buf_ushort[idx_row]));
        }
        break;
      case NC_UINT:
        buf_uint = static_cast<unsigned int*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_UINT), buf_uint[idx_row]));
        }
        break;
      case NC_INT64:
        buf_int64 = static_cast<long long*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_INT64), buf_int64[idx_row]));
        }
        break;
      case NC_UINT64:
        buf_uint64 = static_cast<unsigned long long*> (m_ncvar_crd[m_dim_rows]->m_buf);
        for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
        {
          this->SetRowLabelValue(idx_row, wxString::Format(GetFormat(NC_UINT64), buf_uint64[idx_row]));
        }
        break;
      }//switch
    }
    //coordinate variable does not exist
    else
    {
      for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
      {
        this->SetRowLabelValue(idx_row, wxString::Format(wxT("%d"), idx_row + 1));
      }
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  switch (m_ncvar->m_nc_type)
  {
  case NC_FLOAT:
    buf_float = static_cast<float*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_FLOAT), buf_float[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_DOUBLE:
    buf_double = static_cast<double*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_DOUBLE), buf_double[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_INT:
    buf_int = static_cast<int*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_INT), buf_int[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_SHORT:
    buf_short = static_cast<short*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_SHORT), buf_short[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_CHAR:
    buf_char = static_cast<char*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_CHAR), buf_char[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_BYTE:
    buf_byte = static_cast<signed char*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_BYTE), buf_byte[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_UBYTE:
    buf_ubyte = static_cast<unsigned char*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_UBYTE), buf_ubyte[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_USHORT:
    buf_ushort = static_cast<unsigned short*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_USHORT), buf_ushort[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_UINT:
    buf_uint = static_cast<unsigned int*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_UINT), buf_uint[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_INT64:
    buf_int64 = static_cast<long long*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_INT64), buf_int64[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_UINT64:
    buf_uint64 = static_cast<unsigned long long*> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_UINT64), buf_uint64[idx_buf]));
        idx_buf++;
      }
    }
    break;
  case NC_STRING:
    buf_string = static_cast<char**> (m_ncvar->m_buf);
    for (int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for (int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        this->SetCellValue(idx_row, idx_col, wxString::Format(GetFormat(NC_STRING), buf_string[idx_buf]));
        idx_buf++;
      }
    }
    break;
  }//switch
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxGridLayers::GetFormat
//Provide sprintf() format string for specified netCDF type
//Based on NCO utilities
/////////////////////////////////////////////////////////////////////////////////////////////////////

wxString wxGridLayers::GetFormat(const nc_type typ)
{
  const char fmt_NC_FLOAT[] = "%g";
  const char fmt_NC_DOUBLE[] = "%.12g";
  const char fmt_NC_INT[] = "%i";
  const char fmt_NC_SHORT[] = "%hi";
  const char fmt_NC_CHAR[] = "%c";
  const char fmt_NC_BYTE[] = "%hhi";
  const char fmt_NC_UBYTE[] = "%hhu";
  const char fmt_NC_USHORT[] = "%hu";
  const char fmt_NC_UINT[] = "%u";
  const char fmt_NC_INT64[] = "%lli";
  const char fmt_NC_UINT64[] = "%llu";
  const char fmt_NC_STRING[] = "%s";
  switch (typ)
  {
  case NC_FLOAT:
    return  wxString::FromAscii(fmt_NC_FLOAT);
  case NC_DOUBLE:
    return wxString::FromAscii(fmt_NC_DOUBLE);
  case NC_INT:
    return wxString::FromAscii(fmt_NC_INT);
  case NC_SHORT:
    return wxString::FromAscii(fmt_NC_SHORT);
  case NC_CHAR:
    return wxString::FromAscii(fmt_NC_CHAR);
  case NC_BYTE:
    return wxString::FromAscii(fmt_NC_BYTE);
  case NC_UBYTE:
    return wxString::FromAscii(fmt_NC_UBYTE);
  case NC_USHORT:
    return wxString::FromAscii(fmt_NC_USHORT);
  case NC_UINT:
    return wxString::FromAscii(fmt_NC_UINT);
  case NC_INT64:
    return wxString::FromAscii(fmt_NC_INT64);
  case NC_UINT64:
    return wxString::FromAscii(fmt_NC_UINT64);
  case NC_STRING:
    return wxString::FromAscii(fmt_NC_STRING);
  }
  return wxString::FromAscii("");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::OnItemMenu
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTreeCtrlExplorer::OnItemMenu(wxTreeEvent& event)
{
  wxMenu menu;
  wxTreeItemId item_id = event.GetItem();
  this->SetFocusedItem(item_id);
  wxItemData *item_data = (wxItemData *)GetItemData(item_id);
  if (item_data->m_kind != wxItemData::Variable)
  {
    return;
  }
  menu.Append(ID_TREE_LOAD_ITEM, wxT("&Show"));
  PopupMenu(&menu, event.GetPoint());
  event.Skip();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//wxTreeCtrlExplorer::OnLoadItem
/////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTreeCtrlExplorer::OnLoadItem(wxCommandEvent& WXUNUSED(event))
{
  wxTreeItemId item_id = this->GetFocusedItem();
  wxItemData *item_data = (wxItemData *)GetItemData(item_id);
  assert(item_data->m_kind == wxItemData::Variable);
  ShowVariable(item_data);
}




