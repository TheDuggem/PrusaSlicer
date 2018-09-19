#include "ProgressStatusBar.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include "GUI.hpp"

#include <iostream>

namespace Slic3r {

ProgressStatusBar::ProgressStatusBar(wxWindow *parent, int id):
    self(new wxStatusBar(parent ? parent : GUI::get_main_frame(),
                         id == -1? wxID_ANY : id)),
    m_timer(new wxTimer(self)),
    m_prog (new wxGauge(self,
                       wxGA_HORIZONTAL,
                       100,
                       wxDefaultPosition,
                       wxDefaultSize)),
    m_cancelbutton(new wxButton(self,
                               -1,
                               "Cancel",
                               wxDefaultPosition,
                               wxDefaultSize))
{
    m_prog->Hide();
    m_cancelbutton->Hide();

    self->SetFieldsCount(3);
    int w[] = {-1, 150, 155};
    self->SetStatusWidths(3, w);

    self->Bind(wxEVT_TIMER, [this](const wxTimerEvent&) {
        if (m_prog->IsShown()) m_timer->Stop();
        if(is_busy()) m_prog->Pulse();
    });

    self->Bind(wxEVT_SIZE, [this](wxSizeEvent& event){
        wxRect rect;
        self->GetFieldRect(1, rect);
        auto offset = 0;
        m_cancelbutton->Move(rect.GetX() + offset, rect.GetY() + offset);
        m_cancelbutton->SetSize(rect.GetWidth() - offset, rect.GetHeight());

        self->GetFieldRect(2, rect);
        m_prog->Move(rect.GetX() + offset, rect.GetY() + offset);
        m_prog->SetSize(rect.GetWidth() - offset, rect.GetHeight());

        event.Skip();
    });

    m_cancelbutton->Bind(wxEVT_BUTTON, [this](const wxCommandEvent&) {
        if(m_cancel_cb) m_cancel_cb();
        m_perl_cancel_callback.call();
        m_cancelbutton->Hide();
    });
}

ProgressStatusBar::~ProgressStatusBar() {
    if(m_timer->IsRunning()) m_timer->Stop();
}

int ProgressStatusBar::get_progress() const
{
    return m_prog->GetValue();
}

void ProgressStatusBar::set_progress(int val)
{
    if(!m_prog->IsShown()) show_progress(true);

    if(val == m_prog->GetRange()) {
        m_prog->SetValue(0);
        show_progress(false);
    } else {
        m_prog->SetValue(val);
    }
}

int ProgressStatusBar::get_range() const
{
    return m_prog->GetRange();
}

void ProgressStatusBar::set_range(int val)
{
    if(val != m_prog->GetRange()) {
        m_prog->SetRange(val);
    }
}

void ProgressStatusBar::show_progress(bool show)
{
    m_prog->Show(show);
    m_prog->Pulse();
}

void ProgressStatusBar::start_busy(int rate)
{
    m_busy = true;
    show_progress(true);
    if (!m_timer->IsRunning()) {
        m_timer->Start(rate);
    }
}

void ProgressStatusBar::stop_busy()
{
    m_timer->Stop();
    show_progress(false);
    m_prog->SetValue(0);
    m_busy = false;
}

void ProgressStatusBar::set_cancel_callback(ProgressStatusBar::CancelFn ccb) {
    m_cancel_cb = ccb;
    if(ccb) m_cancelbutton->Show();
    else m_cancelbutton->Hide();
}

void ProgressStatusBar::run(int rate)
{
    if(!m_timer->IsRunning()) {
        m_timer->Start(rate);
    }
}

void ProgressStatusBar::embed(wxFrame *frame)
{
    wxFrame* mf = frame? frame : GUI::get_main_frame();
    mf->SetStatusBar(self);
}

void ProgressStatusBar::set_status_text(const wxString& txt)
{
    self->SetStatusText(wxString::FromUTF8(txt.c_str()));
}

void ProgressStatusBar::show_cancel_button()
{
    m_cancelbutton->Show();
}

void ProgressStatusBar::hide_cancel_button()
{
    m_cancelbutton->Hide();
}

}
