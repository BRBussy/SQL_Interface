
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
	string Day;
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
string old_operating_mode = "NULL";

bool process_received_frames(void);
BYTE *read_Payload(const string &filename, const int &no_of_bytes_in_payload);
template <class T> void rebuild_received_data(BYTE *Data_Payload, const int &Num_Bytes_in_Payload, T& rebuilt_variable);
bool process_received_power_reading(const power_measurement &measurement_received);
void process_schedule_data(const data_base_scheduling_information &database_schedule);
void process_operating_mode(const string &operating_mode);
char day_number(const string &day);

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
	int no_of_entries;
	bool New_Data_Received = false;
	//bool Server_Initialised = false;
	bool loop = true;


	power_measurement my_test_measurement;
	cout << "Size is: " << sizeof(my_test_measurement) << endl;

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
		
		
		//------------CHECK FOR NEW OPERATING MODE TO PROCESS------------------------------
		try
		{
			stmt->execute("USE design_db;");
			res = stmt->executeQuery("SELECT * FROM flags");//res = stmt->executeQuery("SELECT * FROM schedules");
		}
		catch (sql::SQLException e)
		{
			cout << "SQL error. Error message: " << e.what() << endl;
			system("pause");
			exit(1);
		}
		while (res->next())
		{
			string flag_ID = (res->getString("Flag_ID")).c_str();
			if (flag_ID == "operating_mode")
			{
				cout << "Found IT!" << endl;
				string new_operating_mode = (res->getString("Flag_Value")).c_str();
				cout << new_operating_mode << endl;
				if ((old_operating_mode == "NULL") || (old_operating_mode != new_operating_mode))
				{
					cout << "Operating Mode Changed!" << endl;
					process_operating_mode(new_operating_mode);
					old_operating_mode = new_operating_mode;
				}
			}
			
		}
		//------------CHECK FOR NEW SCHEDULING DATA TO PROCESS------------------------------
		try
		{
			stmt->execute("USE design_db;");
			res = stmt->executeQuery("SELECT * FROM flags");//res = stmt->executeQuery("SELECT * FROM schedules");
		}
		catch (sql::SQLException e)
		{
			cout << "SQL error. Error message: " << e.what() << endl;
			system("pause");
			exit(1);
		}
		while (res->next())  //Iterate through to find Unread
		{
			//if (res->getInt("Is_Read") == 0) { //If one is Unread, The schedule has been changed
			//	cout << "Detected that the Schedule has been changed!!" << endl;
			//	Schedule_Changed = true;
			//	break;
			//}
			sql::SQLString flag_ID = res->getString("Flag_ID");
			string flag_ID_string = flag_ID.c_str();
			if (flag_ID_string == "schedule_modified")
			{
				sql::SQLString flag_value = res->getString("Flag_Value");
				string flag_value_string = flag_value.c_str();
				if (flag_value_string == "yes")
				{
					Schedule_Changed = true;
					break;
					cout << flag_value_string << endl;
				}
				
			}
		}
		//Mark that the Schedule Modified Flag Has been acknowledged
		if (Schedule_Changed)
		{
			//Mark Read
			try
			{
				stmt->execute("USE design_db;");
				stmt->execute("UPDATE flags SET Flag_Value = 'no' WHERE Flag_ID = 'schedule_modified';");
			}
			catch (sql::SQLException e)
			{
				cout << "SQL error. Error message: " << e.what() << endl;
				system("pause");
				exit(1);
			}
		}
		
		//------------IF there is new Scheduling Data to Process then Process It-------------
		if (Schedule_Changed) 
		{
			cout << "Reading and Processing Updated Schedule!" << endl << endl;
			
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
				sql::SQLString Day = res->getString("Day");
				string Time_Start_String = Time_Start.c_str();
				string Time_End_String = Time_End.c_str();
				string Day_String = Day.c_str();

				database_schedule.Time_Start = Time_Start_String;
				database_schedule.Time_End = Time_End_String;
				database_schedule.Day = Day_String;
				database_schedule.Device_ID = res->getInt("Device_ID");
				process_schedule_data(database_schedule);
				//Mark Read
				// UPDATE schedules SET Is_Read = 1 WHERE Day = 1;
				ostringstream Mark_Read;
				Mark_Read << "UPDATE schedules SET Is_Read = 1 WHERE "
					<< "Day = '" << database_schedule.Day << "'"
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
			}
			Schedule_Changed = false;
		}
		else 
		{
			cout << "No Change in Schedule." << endl;
		}
		//loop = false;
		Sleep(500);
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
					cout << endl << "Dodgy reading? -->"<< process_received_power_reading(measurement_received) << endl;
					

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
bool process_received_power_reading(const power_measurement &measurement_received)
{
	int second_made = 0;
	int minute_made = 0;
	int year_made = 0;
	int month_made = 5;
	int day_made = 0;
	int hour_made = 0;

	if ((int)measurement_received.when_made.second > 59) {
		return 0;
	}
	else
	{
		second_made = (int)measurement_received.when_made.second;
	}
	if ((int)measurement_received.when_made.minute > 59)
	{
		return 0;
	}
	else
	{
		minute_made = (int)measurement_received.when_made.minute;
	}
	if ((int)measurement_received.when_made.year > 2016)
	{
		return 0;
	}
	else
	{
		year_made = (int)measurement_received.when_made.year;
	}
	if ((int)measurement_received.when_made.month > 5)
	{
		return 0;
	}
	else
	{
		month_made = (int)measurement_received.when_made.month;
	}
	if ((int)measurement_received.when_made.dayOfMonth > 31)
	{
		return 0;
	}
	else
	{
		day_made = (int)measurement_received.when_made.dayOfMonth;
	}
	if ((int)measurement_received.when_made.hour > 31)
	{
		return 0;
	}
	else
	{
		hour_made = (int)measurement_received.when_made.hour;
	}


	cout <<endl<< "Contents of Measurement to Store to dB: " << endl
		<< "ID: " << measurement_received.ID << endl
		<< "Measurement: " << (float)measurement_received.measurement << endl
		<< "Taken On: " << (long long int)measurement_received.when_made.year << "/"
		<< (long int)measurement_received.when_made.month << "/"
		<< (long int)measurement_received.when_made.dayOfMonth << " at "
		<< (long int)measurement_received.when_made.hour << ":"
		<< (long int)measurement_received.when_made.minute << ":"
		<< (long int)measurement_received.when_made.second << endl;	

	ostringstream conversion_stream;
	conversion_stream << "INSERT INTO `power_stats` (`Device_ID`, `Time_Recorded`, `Power_Reading`) VALUES  ("
		<< "'" << measurement_received.ID << "', "
		<< "'" << year_made << "-"
		<< month_made << "-"
		<< day_made << " "
		<< hour_made << ":"
		<< minute_made << ":"
		<< second_made << "', '"
		<< measurement_received.measurement << "');";
	stmt_string = conversion_stream.str();
	return 1;
}
void process_schedule_data(const data_base_scheduling_information &database_schedule)
{
	//cout << database_schedule.Day << endl;
	//cout << database_schedule.Time_Start << endl;
	//cout << database_schedule.Time_End << endl;
	
	char *schedule = NULL;
	schedule = (char*)realloc(schedule, 10*sizeof(char));

	schedule[0] = 'S';
	schedule[1] = day_number(database_schedule.Day);
	schedule[2] = (database_schedule.Time_Start.c_str())[0];
	schedule[3] = (database_schedule.Time_Start.c_str())[1];
	schedule[4] = (database_schedule.Time_Start.c_str())[3];
	schedule[5] = (database_schedule.Time_Start.c_str())[4];
	schedule[6] = (database_schedule.Time_End.c_str())[0];
	schedule[7] = (database_schedule.Time_End.c_str())[1];
	schedule[8] = (database_schedule.Time_End.c_str())[3];
	schedule[9] = (database_schedule.Time_End.c_str())[4];

	for (int i = 0; i < 10; i ++)
	{
		cout << schedule[i];
	}
	cout << endl;
	
	//Save Schedule Info To File:
	ofstream outfile;
	outfile.open("C:\\Users\\Bernard\\Documents\\Buffer_area\\Send_to_Client\\Scheduling_Info.SI",std::fstream::app);
		for (int i = 0; i < 10; i++)
		{
			outfile << schedule[i];
		}
	outfile.close(); //close the file.
	
}
void process_operating_mode(const string &operating_mode)
{
	cout << "Operating Mode to Save is: " << operating_mode << endl;

	//Save Schedule Info To File:
	ofstream outfile;
	outfile.open("C:\\Users\\Bernard\\Documents\\Buffer_area\\Send_to_Client\\Operating_Mode.CM", std::fstream::app);
	outfile << operating_mode;
	outfile.close(); //close the file.
}

char day_number(const string &day)
{
	if (day == "Monday")
		return '0';
	else if (day == "Tuesday")
		return '1';
	else if (day == "Wednesday")
		return '2';
	else if (day == "Thursday")
		return '3';
	else if (day == "Friday")
		return '4';
	else if (day == "Saturday")
		return '5';
	else if (day == "Sunday")
		return '6';
	else
		return '8';
}




/*
if (((Day <= 6) && (Day >= 0))
&& ((Hour_Start <= 23) && (Hour_Start >= 0))
&& ((Hour_End <= 23) && (Hour_End >= 0))
&& ((Minute_Start <= 59) && (Minute_Start >= 0))
&& ((Minute_End <= 59) && (Minute_End >= 0))
&& (Hour_Start <= Hour_End)
&& ((((Hour_Start==Hour_End)&&(Minute_Start <= Minute_End)))|| (Hour_Start != Hour_End)))
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
}*/