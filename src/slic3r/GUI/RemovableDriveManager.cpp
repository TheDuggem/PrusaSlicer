#include "RemovableDriveManager.hpp"
#include <libslic3r/libslic3r.h>

#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>

#if _WIN32
#include <windows.h>
#include <tchar.h>
#include <winioctl.h>
#include <shlwapi.h>

#include <Dbt.h>

#else
// unix, linux & OSX includes
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <glob.h>
#include <pwd.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/process.hpp>
#endif

namespace Slic3r {
namespace GUI { 

wxDEFINE_EVENT(EVT_REMOVABLE_DRIVE_EJECTED, RemovableDriveEjectEvent);
wxDEFINE_EVENT(EVT_REMOVABLE_DRIVES_CHANGED, RemovableDrivesChangedEvent);

#if _WIN32
std::vector<DriveData> RemovableDriveManager::search_for_removable_drives() const
{
	// Get logical drives flags by letter in alphabetical order.
	DWORD drives_mask = ::GetLogicalDrives();

	// Allocate the buffers before the loop.
	std::wstring volume_name;
	std::wstring file_system_name;
	// Iterate the Windows drives from 'C' to 'Z'
	std::vector<DriveData> current_drives;
	// Skip A and B drives.
	drives_mask >>= 2;
	for (char drive = 'C'; drive <= 'Z'; ++ drive, drives_mask >>= 1)
		if (drives_mask & 1) {
			std::string path { drive, ':' };
			UINT drive_type = ::GetDriveTypeA(path.c_str());
			// DRIVE_REMOVABLE on W are sd cards and usb thumbnails (not usb harddrives)
			if (drive_type ==  DRIVE_REMOVABLE) {
				// get name of drive
				std::wstring wpath = boost::nowide::widen(path);
				volume_name.resize(MAX_PATH + 1);
				file_system_name.resize(MAX_PATH + 1);
				BOOL error = ::GetVolumeInformationW(wpath.c_str(), volume_name.data(), sizeof(volume_name), nullptr, nullptr, nullptr, file_system_name.data(), sizeof(file_system_name));
				if (error != 0) {
					volume_name.erase(volume_name.begin() + wcslen(volume_name.c_str()), volume_name.end());
					if (! file_system_name.empty()) {
						ULARGE_INTEGER free_space;
						::GetDiskFreeSpaceExW(wpath.c_str(), &free_space, nullptr, nullptr);
						if (free_space.QuadPart > 0) {
							path += "\\";
							current_drives.emplace_back(DriveData{ boost::nowide::narrow(volume_name), path });
						}
					}
				}
			}
		}
	return current_drives;
}

// Called from UI therefore it blocks the UI thread.
// It also blocks updates at the worker thread.
// Win32 implementation.
void RemovableDriveManager::eject_drive()
{
	if (m_last_save_path.empty())
		return;

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	tbb::mutex::scoped_lock lock(m_drives_mutex);
	auto it_drive_data = this->find_last_save_path_drive_data();
	if (it_drive_data != m_current_drives.end()) {
		// get handle to device
		std::string mpath = "\\\\.\\" + m_last_save_path;
		mpath = mpath.substr(0, mpath.size() - 1);
		HANDLE handle = CreateFileW(boost::nowide::widen(mpath).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			std::cerr << "Ejecting " << mpath << " failed " << GetLastError() << " \n";
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(*it_drive_data, false)));
			return;
		}
		DWORD deviceControlRetVal(0);
		//these 3 commands should eject device safely but they dont, the device does disappear from file explorer but the "device was safely remove" notification doesnt trigger.
		//sd cards does  trigger WM_DEVICECHANGE messege, usb drives dont
		DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		// some implemenatations also calls IOCTL_STORAGE_MEDIA_REMOVAL here but it returns error to me
		BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		if (error == 0) {
			CloseHandle(handle);
			BOOST_LOG_TRIVIAL(error) << "Ejecting " << mpath << " failed " << deviceControlRetVal << " " << GetLastError() << " \n";
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(*it_drive_data, false)));
			return;
		}
		CloseHandle(handle);
		assert(m_callback_evt_handler);
		if (m_callback_evt_handler) 
			wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair< DriveData, bool >(std::move(*it_drive_data), true)));
		m_current_drives.erase(it_drive_data);
	}
}

std::string RemovableDriveManager::get_removable_drive_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	tbb::mutex::scoped_lock lock(m_drives_mutex);
	if (m_current_drives.empty())
		return std::string();
	std::size_t found = path.find_last_of("\\");
	std::string new_path = path.substr(0, found);
	int letter = PathGetDriveNumberW(boost::nowide::widen(new_path).c_str());
	for (const DriveData &drive_data : m_current_drives) {
		char drive = drive_data.path[0];
		if (drive == 'A' + letter)
			return path;	
	}
	return m_current_drives.front().path;
}

std::string RemovableDriveManager::get_removable_drive_from_path(const std::string& path)
{
	tbb::mutex::scoped_lock lock(m_drives_mutex);
	std::size_t found = path.find_last_of("\\");
	std::string new_path = path.substr(0, found);
	int letter = PathGetDriveNumberW(boost::nowide::widen(new_path).c_str());	
	for (const DriveData &drive_data : m_current_drives) {
		assert(! drive_data.path.empty());
		if (drive_data.path.front() == 'A' + letter)
			return drive_data.path;
	}
	return std::string();
}

// Called by Win32 Volume arrived / detached callback.
void RemovableDriveManager::volumes_changed()
{
	if (m_initialized) {
		// Signal the worker thread to wake up and enumerate removable drives.
	    m_wakeup = true;
		m_thread_stop_condition.notify_all();
	}
}

#else

namespace search_for_drives_internal 
{
	static bool compare_filesystem_id(const std::string &path_a, const std::string &path_b)
	{
		struct stat buf;
		stat(path_a.c_str() ,&buf);
		dev_t id_a = buf.st_dev;
		stat(path_b.c_str() ,&buf);
		dev_t id_b = buf.st_dev;
		return id_a == id_b;
	}

	void inspect_file(const std::string &path, const std::string &parent_path, std::vector<DriveData> &out)
	{
		//confirms if the file is removable drive and adds it to vector

		//if not same file system - could be removable drive
		if (! compare_filesystem_id(path, parent_path)) {
			//free space
			boost::filesystem::space_info si = boost::filesystem::space(path);
			if (si.available != 0) {
				//user id
				struct stat buf;
				stat(path.c_str(), &buf);
				uid_t uid = buf.st_uid;
				std::string username(std::getenv("USER"));
				struct passwd *pw = getpwuid(uid);
				if (pw != 0 && pw->pw_name == username)
					out.emplace_back(DriveData{ boost::filesystem::basename(boost::filesystem::path(path)), path });
			}
		}
	}

	static void search_path(const std::string &path, const std::string &parent_path, std::vector<DriveData> &out)
	{
	    glob_t globbuf;
		globbuf.gl_offs = 2;
		int error = glob(path.c_str(), GLOB_TILDE, NULL, &globbuf);
		if (error == 0) {
			for (size_t i = 0; i < globbuf.gl_pathc; ++ i)
				inspect_file(globbuf.gl_pathv[i], parent_path, out);
		} else {
			//if error - path probably doesnt exists so function just exits
			//std::cout<<"glob error "<< error<< "\n";
		}
		globfree(&globbuf);
	}
}

std::vector<DriveData> RemovableDriveManager::search_for_removable_drives() const
{
	std::vector<DriveData> current_drives;

#if __APPLE__

	this->list_devices(current_drives);

#else

    //search /media/* folder
	search_for_drives_internal::search_path("/media/*", "/media", current_drives);

	//search_path("/Volumes/*", "/Volumes");
    std::string path(std::getenv("USER"));
	std::string pp(path);

	//search /media/USERNAME/* folder
	pp = "/media/"+pp;
	path = "/media/" + path + "/*";
	search_for_drives_internal::search_path(path, pp, current_drives);

	//search /run/media/USERNAME/* folder
	path = "/run" + path;
	pp = "/run"+pp;
	search_for_drives_internal::search_path(path, pp, current_drives);
#endif

	return current_drives;
}

// Called from UI therefore it blocks the UI thread.
// It also blocks updates at the worker thread.
// Unix & OSX implementation.
void RemovableDriveManager::eject_drive()
{
	if (m_last_save_path.empty())
		return;

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	tbb::mutex::scoped_lock lock(m_drives_mutex);
	auto it_drive_data = this->find_last_save_path_drive_data();
	if (it_drive_data != m_current_drives.end()) {
		std::string correct_path(m_last_save_path);
#ifndef __APPLE__
		for (size_t i = 0; i < correct_path.size(); ++i) 
        	if (correct_path[i]==' ') {
				correct_path = correct_path.insert(i,1,'\\');
        		++ i;
        	}
#endif
		//std::cout<<"Ejecting "<<(*it).name<<" from "<< correct_path<<"\n";
		// there is no usable command in c++ so terminal command is used instead
		// but neither triggers "succesful safe removal messege"
        	BOOST_LOG_TRIVIAL(info) << "Ejecting started";
        	boost::process::ipstream istd_err;
    		boost::process::child child(
#if __APPLE__		
			boost::process::search_path("diskutil"), "eject", correct_path.c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
			//Another option how to eject at mac. Currently not working.
			//used insted of system() command;
			//this->eject_device(correct_path);
#else
    		boost::process::search_path("umount"), correct_path.c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
#endif
		std::string line;
		while (child.running() && std::getline(istd_err, line)) {
			BOOST_LOG_TRIVIAL(trace) << line;
		}
		// wait for command to finnish (blocks ui thread)
		child.wait();
    	int err = child.exit_code();
    	if (err) {
    		BOOST_LOG_TRIVIAL(error) << "Ejecting failed";
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(*it_drive_data, false)));
    		return;
    	}
		BOOST_LOG_TRIVIAL(info) << "Ejecting finished";

		assert(m_callback_evt_handler);
		if (m_callback_evt_handler) 
			wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(std::move(*it_drive_data), true)));
		m_current_drives.erase(it_drive_data);
	}
}

std::string RemovableDriveManager::get_removable_drive_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	std::size_t found = path.find_last_of("/");
	std::string new_path = found == path.size() - 1 ? path.substr(0, found) : path;

	tbb::mutex::scoped_lock lock(m_drives_mutex);
	for (const DriveData &data : m_current_drives)
		if (search_for_drives_internal::compare_filesystem_id(new_path, data.path))
			return path;
	return m_current_drives.empty() ? std::string() : m_current_drives.front().path;
}

std::string RemovableDriveManager::get_removable_drive_from_path(const std::string& path)
{
	std::size_t found = path.find_last_of("/");
	std::string new_path = found == path.size() - 1 ? path.substr(0, found) : path;
    // trim the filename
    found = new_path.find_last_of("/");
    new_path = new_path.substr(0, found);
    
	// check if same filesystem
	tbb::mutex::scoped_lock lock(m_drives_mutex);
	for (const DriveData &drive_data : m_current_drives)
		if (search_for_drives_internal::compare_filesystem_id(new_path, drive_data.path))
			return drive_data.path;
	return std::string();
}
#endif

void RemovableDriveManager::init(wxEvtHandler *callback_evt_handler)
{
	assert(! m_initialized);
	assert(m_callback_evt_handler == nullptr);

	if (m_initialized)
		return;

	m_initialized = true;
	m_callback_evt_handler = callback_evt_handler;

#if __APPLE__
    this->register_window_osx();
#endif

#ifdef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#else // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	// Don't call update() manually, as the UI triggered APIs call this->update() anyways.
	m_thread = boost::thread((boost::bind(&RemovableDriveManager::thread_proc, this)));
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
}

void RemovableDriveManager::shutdown()
{
#if __APPLE__
	this->unregister_window_osx();
#endif

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
    if (m_thread.joinable()) {
    	// Stop the worker thread, if running.
		{
			// Notify the worker thread to cancel wait on detection polling.
			std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
			m_stop = true;
		}
		m_thread_stop_condition.notify_all();
		// Wait for the worker thread to stop.
		m_thread.join();
		m_stop = false;
	}
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	m_initialized = false;
	m_callback_evt_handler = nullptr;
}

bool RemovableDriveManager::set_and_verify_last_save_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

	m_last_save_path = this->get_removable_drive_from_path(path);
	m_exporting_finished = false;
	return ! m_last_save_path.empty();
}

RemovableDriveManager::RemovableDrivesStatus RemovableDriveManager::status()
{

	RemovableDriveManager::RemovableDrivesStatus out;
	{
		tbb::mutex::scoped_lock lock(m_drives_mutex);
		out.has_eject = this->find_last_save_path_drive_data() != m_current_drives.end();
		out.has_removable_drives = ! m_current_drives.empty();
	}
	if (! out.has_eject) 
		m_last_save_path.clear();
	out.has_eject = out.has_eject && m_exporting_finished;
	return out;
}

// Update is called from thread_proc() and from most of the public methods on demand.
void RemovableDriveManager::update()
{
	tbb::mutex::scoped_lock inside_update_lock;
#ifdef _WIN32
	// All wake up calls up to now are now consumed when the drive enumeration starts.
	m_wakeup = false;
#endif // _WIN32
	if (inside_update_lock.try_acquire(m_inside_update_mutex)) {
		// Got the lock without waiting. That means, the update was not running.
		// Run the update.
		std::vector<DriveData> current_drives = this->search_for_removable_drives();
		// Post update events.
		tbb::mutex::scoped_lock lock(m_drives_mutex);
		std::sort(current_drives.begin(), current_drives.end());
		if (current_drives != m_current_drives) {
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDrivesChangedEvent(EVT_REMOVABLE_DRIVES_CHANGED));
		}
		m_current_drives = std::move(current_drives);
	} else {
		// Acquiring the m_iniside_update lock failed, therefore another update is running.
		// Just block until the other instance of update() finishes.
		inside_update_lock.acquire(m_inside_update_mutex);
	}
}

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
void RemovableDriveManager::thread_proc()
{
	// Signal the worker thread to update initially.
#ifdef _WIN32
    m_wakeup = true;
#endif // _WIN32

	for (;;) {
		// Wait for 2 seconds before running the disk enumeration.
		// Cancellable.
		{
			std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
#ifdef _WIN32
			// Reacting to updates by WM_DEVICECHANGE and WM_USER_MEDIACHANGED
			m_thread_stop_condition.wait(lck, [this]{ return m_stop || m_wakeup; });
#else
			m_thread_stop_condition.wait_for(lck, std::chrono::seconds(2), [this]{ return m_stop; });
#endif
		}
		if (m_stop)
			// Stop the worker thread.
			break;
		// Update m_current drives and send out update events.
		this->update();
	}
}
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

std::vector<DriveData>::const_iterator RemovableDriveManager::find_last_save_path_drive_data() const
{
	return Slic3r::binary_find_by_predicate(m_current_drives.begin(), m_current_drives.end(),
		[this](const DriveData &data){ return data.path < m_last_save_path; }, 
		[this](const DriveData &data){ return data.path == m_last_save_path; });
}

}} // namespace Slic3r::GUI
