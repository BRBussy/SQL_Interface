
#include "stdafx.h"
#undef UNICODE
#define WIN32_LEAN_AND_MEAN
/* Standard C++ includes */
#include <stdlib.h>
#include <iostream>
#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <Windows.h>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string>
#include <tchar.h> 
#include <strsafe.h>
#include <sstream>
#include <time.h>

#pragma comment(lib, "User32.lib")
using namespace std;

//Special Data Types
//Scheduling Information Structure
struct scheduling_information { //Declare scheduling_information struct type
	int ID; //Device ID
	bool hours_on_off[7][24][60]; //hours_on_off[Days, Hours, Minutes]
};
struct data_base_scheduling_information {//Declare data_base_scheduling_information struct type
	string Time_Start;
	string Time_End;
	int Day;
	int Device_ID;
};
//Time and Date Structure
struct time_and_date { //Declare time_and_date struct type
	uint16_t year;
	uint8_t month;
	uint8_t dayOfMonth;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
};
//Power Measurement Structure
struct power_measurement { //Declare power_measurement struct type
	float measurement; //Actual Power Measurement
	time_and_date when_made; //when this reading was taken
	int ID; //The Major Appliance ID
};
//Major Appliance Status Structure
struct major_appliance_status { //Declare major_appliance_status struct type
	bool on_off; //Current on/off Status
	power_measurement latest_measurement; //Most recent power reading
};

// Specify our connection target and credentials
const string server = "tcp://localhost:3306";
const string username = "root";
const string password = ""; // No password - NEVER DO THIS ON A PRODUCTION SERVER!
string stmt_string;

bool process_received_frames(void);
BYTE *read_Payload(const string &filename, const int &no_of_bytes_in_payload);
template <class T> void rebuild_received_data(BYTE *Data_Payload, const int &Num_Bytes_in_Payload, T& rebuilt_variable);
void process_received_power_reading(const power_measurement &measurement_received);
void process_schedule_data(scheduling_information &device_schedule, const data_base_scheduling_information &database_schedule);

void main(void)
{
	
	//MySQL Database Access Variables
	sql::Driver     *driver; // Create a pointer to a MySQL driver object
	sql::Connection *dbConn; // Create a pointer to a database connection object
	sql::Statement  *stmt;   // Create a pointer to a Statement object to hold our SQL commands
	sql::ResultSet  *res;    // Create a pointer to a ResultSet object to hold the results of any queries we run
	//Other Program Variables
	//bool Client_Connected = false;
	bool Schedule_Changed = false;
	bool New_Data_Received = false;
	//bool Server_Initialised = false;
	bool loop = true;

	//--------------SETUP CONNECTION TO DATABASE----------------------
	//Try to get a driver to use
	try {
		driver = get_driver_instance();
	}
	catch (sql::SQLException e)
	{
		cout << "Could not get a database driver. Error message: " << e.what() << endl;
		system("pause");
		exit(1);
	}
	//Try to Connect to Database
	try
	{
		dbConn = driver->connect("tcp://localhost:3306", "root", "");
	}
	catch (sql::SQLException e)
	{
		cout << "Could not connect to database. Error message: " << e.what() << endl;
		system("pause");
		exit(1);
	}
	stmt = dbConn->createStatement();

	do {
		//------------CHECK FOR NEW FRAMES TO DECODE and PROCESS----------------------------
		if (process_received_frames())
		{
			try
			{
				stmt->execute("USE design_db");
				cout << stmt_string << endl;
				stmt->execute(stmt_string.c_str());
			}
			catch (sql::SQLException e)
			{
				cout << "SQL error. Error message: " << e.what() << endl;
				system("pause");
				exit(1);
			}
		}
		//------------CHECK FOR NEW SCHEDULING DATA TO PROCESS------------------------------
		try
		{
			stmt->execute("USE design_db;");
			res = stmt->executeQuery("SELECT * FROM schedules");
		}
		catch (sql::SQLException e)
		{
			cout << "SQL error. Error message: " << e.what() << endl;
			system("pause");
			exit(1);
		}
		while (res->next())  //Iterate through to find Unread
		{
			if (res->getInt("Is_Read") == 0) { //If one is Unread, The schedule has been changed
				cout << "Detected that the Schedule has been changed!!" << endl;
				Schedule_Changed = true;
				break;
			}
		}
		//------------IF there is new Scheduling Data to Process then Process It-------------
		if (Schedule_Changed) {
			cout << "Reading and Processing Updated Schedule!" << endl << endl;
			scheduling_information device_schedule;
			//Initialise all to False
			for (int day = 0; day <= 6; day++) {
				for (int hour = 0; hour <= 23; hour++) {
					for (int minute = 0; minute <= 59; minute++) {
						device_schedule.hours_on_off[day][hour][minute] = false;
					}
				}
			}
			
			try
			{
				stmt->execute("USE design_db;");
				res = stmt->executeQuery("SELECT * FROM schedules");
			}
			catch (sql::SQLException e)
			{
				cout << "SQL error. Error message: " << e.what() << endl;
				system("pause");
				exit(1);
			}

			while (res->next())
			{
				data_base_scheduling_information database_schedule;
				sql::SQLString Time_Start = res->getString("Time_Start");
				sql::SQLString Time_End = res->getString("Time_End");
				string Time_Start_String = Time_Start.c_str();
				string Time_End_String = Time_End.c_str();
				database_schedule.Time_Start = Time_Start_String;
				database_schedule.Time_End = Time_End_String;
				database_schedule.Day = res->getInt("Day");
				database_schedule.Device_ID = res->getInt("Device_ID");
				process_schedule_data(device_schedule, database_schedule);
				//Mark Read
				// UPDATE schedules SET Is_Read = 1 WHERE Day = 1;
				ostringstream Mark_Read;
				Mark_Read << "UPDATE schedules SET Is_Read = 1 WHERE "
					<< "Day = " << database_schedule.Day
					<< " AND Time_Start = '" << Time_Start_String << "';";
				try
				{
					stmt->execute("USE design_db;");
					stmt->execute(Mark_Read.str().c_str());
				}
				catch (sql::SQLException e)
				{
					cout << "SQL error. Error message: " << e.what() << endl;
					system("pause");
					exit(1);
				}

				//Print Out The Schedule
				/*if (!res->next()) {
				for (int day = 0; day <= 6; day++) {
				cout << "Day " << day << ":" << endl;
				for (int hour = 0; hour <= 23; hour++) {
				cout << "Hour " << hour << ":" << endl;
				for (int minute = 0; minute <= 59; minute++) {
				cout << device_schedule.hours_on_off[day][hour][minute];
				}
				cout << endl;
				}
				}
				}
				res->previous();*/
			}
			Schedule_Changed = false;
		}
		else {
			cout << "No Change in Schedule." << endl << endl;
		}
		//loop = false;
		Sleep(2000);
	} while (loop);

	//Clean up after ourselves
	delete dbConn;
	delete  stmt; 
	delete res;
	exit(1);
}

bool process_received_frames(void)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	LARGE_INTEGER filesize;
	DWORD dwError = 0;

	BYTE *Data_Payload = NULL;
	int no_of_bytes_in_payload = 0;

	bool Power_Readings, Commands, Statuses;

	//Look for power readings i.e. files of type *.prdat
	hFind = FindFirstFile("C:\\Users\\Bernard\\Documents\\Buffer_area\\*.prdat", &FindFileData);
	if (INVALID_HANDLE_VALUE == hFind)
	{
		cout << "No Power Reading Frames in Directory" << endl;
		Power_Readings = false;
	}
	else //There are some power reading Frames
	{
		//Iterate Through Directory and process First Stored Power Frame
			if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
			}
			else
			{
				Power_Readings = true;
				string full_file_location = "C:\\Users\\Bernard\\Documents\\Buffer_area\\";
				full_file_location += FindFileData.cFileName;
				cout << "File is: " << full_file_location << endl;
				filesize.LowPart = FindFileData.nFileSizeLow;
				filesize.HighPart = FindFileData.nFileSizeHigh;
				Data_Payload = read_Payload(FindFileData.cFileName, filesize.QuadPart); //Read from file
				if (Data_Payload != NULL) {
					power_measurement measurement_received;
					rebuild_received_data(Data_Payload, filesize.QuadPart, measurement_received);
					process_received_power_reading(measurement_received);

					if (remove(full_file_location.c_str()))
						cout << "Error deleting file" << endl;
					else
						cout << "File successfully deleted" << endl;
					return 1;
				}
			}
	}

	//Look for commands i.e. files of type *.cmdat
	hFind = FindFirstFile("C:\\Users\\Bernard\\Documents\\Buffer_area\\*.cmdat", &FindFileData);
	if (INVALID_HANDLE_VALUE == hFind)
	{
		cout << "No Commands in Directory" << endl;
		Commands = false;
	}
	else //There are Command Frames
	{
		//Iterate Through Directory and process each command frame
			if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
			}
			else
			{
				Commands = true;
				filesize.LowPart = FindFileData.nFileSizeLow;
				filesize.HighPart = FindFileData.nFileSizeHigh;
				cout << FindFileData.cFileName << " bytes " << filesize.QuadPart << endl;
			}
	}

	//Look for statuses i.e. files of type *.stdat
	hFind = FindFirstFile("C:\\Users\\Bernard\\Documents\\Buffer_area\\*.stdat", &FindFileData);
	if (INVALID_HANDLE_VALUE == hFind)
	{
		cout << "No Statuses in Directory" << endl;
		Statuses = false;
	}
	else //There are status Frames
	{
		//Iterate Through Directory and status each command frame
			if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
			}
			else
			{
				Statuses = true;
				filesize.LowPart = FindFileData.nFileSizeLow;
				filesize.HighPart = FindFileData.nFileSizeHigh;
				cout << FindFileData.cFileName << " bytes " << filesize.QuadPart << endl;
			}
	}
	
	return 0;
}


BYTE *read_Payload(const string &filename, const int &no_of_bytes_in_payload)
{
	BYTE *Data_Payload = NULL;
	//cout << "Filename: " << filename << " size " << no_of_bytes_in_payload << endl;
	Data_Payload = (BYTE*)realloc(Data_Payload, no_of_bytes_in_payload*sizeof(BYTE)); //Create array for bytes

	string file_to_open = "C:\\Users\\Bernard\\Documents\\Buffer_area\\";
	file_to_open += filename;
	
	if (Data_Payload != NULL)
	{
		// open a file in read mode.
		ifstream infile;
		infile.open(file_to_open.c_str());
		for (int i = 0; i < no_of_bytes_in_payload; i++) {
			Data_Payload[i] = infile.get();
		}
		infile.close();
	}
	return Data_Payload;
}

//Data Processing Functions
template <class T> void rebuild_received_data(BYTE *Data_Payload, const int &Num_Bytes_in_Payload, T& rebuilt_variable)
{
	BYTE *ptr_to_rebuilt_variable_bytes = (BYTE*)(void*)(&rebuilt_variable);
	for (int i = 0; i < Num_Bytes_in_Payload; i++) {
		ptr_to_rebuilt_variable_bytes[i] = Data_Payload[i];
	}
}
void process_received_power_reading(const power_measurement &measurement_received)
{
	cout << "Contents of First Measurement: " << endl
		<< "ID: " << measurement_received.ID << endl
		<< "Measurement: " << (float)measurement_received.measurement << endl
		<< "Taken On: " << (int)measurement_received.when_made.year << "/"
		<< (int)measurement_received.when_made.month << "/"
		<< (int)measurement_received.when_made.dayOfMonth << " at "
		<< (int)measurement_received.when_made.hour << ":"
		<< (int)measurement_received.when_made.minute << ":"
		<< (int)measurement_received.when_made.second << endl;	

	ostringstream conversion_stream;
	conversion_stream << "INSERT INTO `power_stats` (`Device_ID`, `Time_Recorded`, `Power_Reading`) VALUES  ("
		<< "'" << measurement_received.ID << "', "

		<< "'" << (int)measurement_received.when_made.year << "-"
		<< (int)measurement_received.when_made.month << "-"
		<< (int)measurement_received.when_made.dayOfMonth << " "
		<< (int)measurement_received.when_made.hour << ":"
		<< (int)measurement_received.when_made.minute << ":"
		<< (short int)measurement_received.when_made.second << "'" << ", "

		<< "'" << measurement_received.measurement << "');";
	stmt_string = conversion_stream.str();
}

void process_schedule_data(scheduling_information &device_schedule, const data_base_scheduling_information &database_schedule)
{
	int Hour_Start = stoi(database_schedule.Time_Start.substr(0, 2));
	int Hour_End = stoi(database_schedule.Time_End.substr(0, 2));
	int Minute_Start = stoi(database_schedule.Time_Start.substr(3, 4));
	int Minute_End = stoi(database_schedule.Time_End.substr(3, 4));
	int Day = database_schedule.Day;
	
	if (((Day <= 6) && (Day >= 0))
		&& ((Hour_Start <= 23) && (Hour_Start >= 0))
		&& ((Hour_End <= 23) && (Hour_End >= 0))
		&& ((Minute_Start <= 59) && (Minute_Start >= 0))
		&& ((Minute_End <= 59) && (Minute_End >= 0))
		&& (Hour_Start <= Hour_End)
		&& ((((Hour_Start==Hour_End)&&(Minute_Start > Minute_End)))|| (Hour_Start != Hour_End)))
	{
		device_schedule.ID = database_schedule.Device_ID;
		//Make True according to Day Schedule
		for (int hour = Hour_Start; hour <= Hour_End; hour++)
		{
			if (Hour_Start != Hour_End) {
				if (hour == Hour_Start) {
					for (int minute = Minute_Start; minute <= 59; minute++) {
						device_schedule.hours_on_off[Day][hour][minute] = true;
					}
				}
				else if (hour == Hour_End) {
					for (int minute = 0; minute <= Minute_End; minute++)
					{
						device_schedule.hours_on_off[Day][hour][minute] = true;
					}
				}
				else { //Hour_Start < hour < Hour_End
					for (int minute = 0; minute <= 59; minute++) {
						device_schedule.hours_on_off[Day][hour][minute] = true;
					}
				}
			}
			else { //Hour_Start == Hour_End
				for (int minute = Minute_Start; minute <= Minute_End; minute++) {
					device_schedule.hours_on_off[Day][hour][minute] = true;
				}
			}
		}
	}
	else {
		//cout << "Invalid Database Entry!" << endl;
		//cout << "Day: " << Day << " Start Time: " << database_schedule.Time_Start << " End Time: " << database_schedule.Time_End << endl;
		return;
	}

	//Save Schedule Info To File:
	ofstream outfile;
	outfile.open("C:\\Users\\Bernard\\Documents\\Buffer_area\\Scheduling_Info.SI");

	BYTE *ptr_to_Scheduling_info_bytes = (BYTE*)(void*)(&device_schedule);
	for (int i = 0; i < sizeof(device_schedule); i++)
	{
		outfile << ptr_to_Scheduling_info_bytes[i];
	}
	outfile.close(); //close the file.
	
}
