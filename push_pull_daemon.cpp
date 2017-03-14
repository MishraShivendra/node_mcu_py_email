// Notice: All parts of this code is protected
// with following license:
// https://github.com/MishraShivendra/node_mcu_py_email/blob/master/LICENSE.md
// Corporate/commercial usage is restricted.
// 
// Author : Shivendra Mishra
// Env: gcc 5.4 on Linux Ubuntu 16.04 

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <new>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <curl/curl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <err.h>
#include "mysql_connection.h"

#define SQL_ADDRESS "sql_address"
#define SQL_USER "sql_user"
#define SQL_PASSWORD "sql_password"
#define NODE_ADDRESS "node_address"
#define NODE_PORT "node_port"

using namespace std;

class pp_daemon
{
	CURL *curl;
	CURLcode curl_res;
	string data;
	sql::Driver *driver;
	sql::Connection *con;
	sql::Statement *stmt;
	sql::ResultSet *res;
	vector <string> temp_values;
	map<string, string> valid_attrs;

	public:
		pp_daemon( int argc, char** argv );
		~pp_daemon( void );
		void conn_and_get( void );
		void get_temperature( const string& sensor_number );
		string push_data_to_db( void );
		void read_set_conf( int argc, char** argv );
		void parse_cli_load_conf( string file_name, int argc,
					  char** cli_argv );
		void show_help ( void );
		void load_conf_attr( string &file_name );
		bool test_if_file_exists( string &file_name );
		string get_home_dir( void );
		bool test_if_dir_exists( string& path );
		void show_args();
		void cleanup();		
};

pp_daemon::pp_daemon( int argc, char** argv )
{
	valid_attrs = { {NODE_ADDRESS, ""}, { NODE_PORT, "" }, 
			{SQL_ADDRESS, ""}, {SQL_USER, ""},
			{SQL_PASSWORD,""} 
		      };
	
	// Load config
	read_set_conf( argc, argv );	
	// Initialize libcurl 
	curl = curl_easy_init();
	if( !curl ) {
		cerr<<"Failed to init curl"<<endl;
	}
	// Create a database connection 
	driver = get_driver_instance();
	con = driver->connect( valid_attrs.at(SQL_ADDRESS), 
			       valid_attrs.at(SQL_USER), 
			       valid_attrs.at(SQL_PASSWORD) );
	// Connect to the MySQL temperature database
	con->setSchema("temp_data");
	show_args();	
}

pp_daemon::~pp_daemon( void )
{
	delete res;
	delete stmt;
	delete con;
	curl_easy_cleanup(curl);
}

static size_t temp_value_write_callback( void *contents, size_t size, 
					     size_t nmemb, string* data )
{
    	size_t newLength = size*nmemb;
    	size_t oldLength = data->size();
#ifdef DEBUG
    	cout<<"Call back Executed."<<endl;
#endif
	try {
        	data->resize(oldLength + newLength);
    	} catch(bad_alloc &e) {
        	//handle memory problem
		cerr<<"Failed to allocate memory"<<e.what()<<endl;
		return 0;
	}

	copy((char*)contents,(char*)contents+newLength,data->begin()+oldLength);
	return size*nmemb;
}


void pp_daemon::get_temperature( const string& sensor_name )
{

	string url = "http://" + valid_attrs.at(NODE_ADDRESS) + ":" + 
		     valid_attrs.at(NODE_PORT) + "/temp/sensor=" + 
		     sensor_name; 
	if(curl) {
#ifdef DEBUG
		curl_easy_setopt (curl, CURLOPT_VERBOSE, 1L);
#endif
		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, 
				  temp_value_write_callback);
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, 
				  (void *) &data);	
		curl_easy_setopt(curl, CURLOPT_URL,url.c_str());
 		curl_res = curl_easy_perform(curl);
		if(curl_res != CURLE_OK) {
			cerr<<"curl_easy_perform() failed:"
			    <<curl_easy_strerror(curl_res);
		}
 	}
}

void pp_daemon::conn_and_get( void )
{
	// I have 1 to whatever number of values to get.
	for( int i = 0; i < 4; ++i ) {
		ostringstream ios;
		ios<<i+1;
		get_temperature( ios.str() );
		while( data == "" );
#ifdef DEBUG
		cout<<"Data["<<i<<"]:"<<data<<endl;
#endif
		temp_values.push_back( data );
		data = "";
	}
}
void pp_daemon::show_args( void )
{
	for( map<string,string>::iterator i = valid_attrs.begin(); 
	     i != valid_attrs.end(); ++i ) {
		cout<<"Read \""<<i->first<<"\" as \""<<i->second<<"\""<<endl;
	}

}

string pp_daemon::push_data_to_db( void )
{
	string ID;
	try {
		
		stmt = con->createStatement();
		
		// We will get the last id
		res = stmt->executeQuery( "select ID from temperature order"\
					  " by ID desc limit 1;" );
		while( res->next() ) {
			ID = res->getString("ID");
		}
	} catch (sql::SQLException &e) {
		cout<<"Failed to get current ID:"<<e.what()<<endl;
	}
	{
		int id = atoi(ID.c_str()) + 1;
		ostringstream id_oss;
		id_oss<<id;
		ID = id_oss.str();
	}

	try{
		// We will assume that a table already exists in the 
		// DB and we are just making entry to it .
#ifdef DEBUG
		cout<<"Readings are:"<<temp_values.at(0)<<" "<<temp_values.at(1)
		    <<" "<<temp_values.at(2)<<" "<<temp_values.at(3)<<" "
		    <<"#"<<ID<<endl;
#endif
		stmt->execute(
			"INSERT INTO temperature ( time_stamp," \
						  "temp_1,"     \
						  "temp_2,"     \
						  "temp_3,"     \
						  "temp_4,"     \
						  "ID )"	\
					"values  ( CURRENT_TIMESTAMP," + 
						  temp_values.at(0)  + "," +
						  temp_values.at(1)  + "," +
						  temp_values.at(2)  + "," +
						  temp_values.at(3)  + "," +
						  ID + 
						");");
	} catch (sql::SQLException &e) {
		cerr<<"Failed to push data to DB:"
		    <<"["<<e.getErrorCode()<<"]"<<e.what()
		    <<endl;
	}
}

bool pp_daemon::test_if_file_exists( string &file_name ) 
{
	ifstream file_tester( file_name );
	return file_tester.good();
}

void pp_daemon::load_conf_attr( string &file_name )
{
	ifstream conf_reader( file_name );
	string attr_line;
	while( getline( conf_reader, attr_line ) ) {
		for ( map<string,string>::iterator i = valid_attrs.begin(); 
		      i != valid_attrs.end(); ++i ) {
			size_t fi = attr_line.find(i->first);
			size_t end_i = attr_line.find("=");
			if( fi != string::npos ) {
				i->second = attr_line[end_i + 1];
			}
		}
	}
}

void pp_daemon::show_help ( void ) {
	cout <<"\nPP App options:\n"<<endl;
	cout <<"-h, --help         Show this help and exit."<<endl;
	cout <<"-A, --nodeaddress  Node address"<<endl;
	cout <<"-P, --nodeport     Node port"<<endl;
	cout <<"-a, --sqladdress   "<<endl;
	cout <<"-u, --sqluser       "<<endl;
	cout <<"-d, --sqlpassword  "<<endl;
	cout <<"-D, --debug        Shows configuration parameter read."
	     <<endl;
}


void pp_daemon::parse_cli_load_conf( string file_name, int argc, 
				     char** argv )
{
	const char *short_option = "A:P:a:u:d:hD";
	struct option long_options[] = {
		{"nodeaddress", 1, NULL, 'A'},
		{"nodeport",    1, NULL, 'P'},
		{"sqladdress",  1, NULL, 'a'},
		{"sqluer",      1, NULL, 'u'},
		{"sqlpassword", 1, NULL, 'd'},
		{"help",        0, NULL, 'h'},
		{"debug",       0, NULL, 'D'},
		{NULL,          0, NULL,  0}
	};
	int option;
	while ( (option = getopt_long( argc, argv, short_option, 
					long_options, NULL ) ) > 0 ) {
		switch ( option ) {
			case 'A':
				valid_attrs[NODE_ADDRESS] = string(optarg);
				break;
			case 'P':
				valid_attrs[NODE_PORT] = string(optarg);
				break;
			case 'a':
				valid_attrs[SQL_ADDRESS] = string(optarg);
				break;
			case 'u':
				valid_attrs[SQL_USER] = string(optarg);
				break;
			case 'd':
				valid_attrs[SQL_PASSWORD] = string(optarg);
				break;
			case 'h':
				show_help();
				break;
			case ':':
				cout<<"Argument missing."<<endl;
				exit(0);
			case '?':
				ostringstream o_oss;
				o_oss<<option;
				cout<<"Invalid option:"<<o_oss.str()<<endl;
				exit(0);
		} 
	}
	
}

bool pp_daemon::test_if_dir_exists( string& path )
{
	struct stat info;
	if( stat( path.c_str(), &info ) == 0 ) {
		return true;
	}
	return false;    	
}

string pp_daemon::get_home_dir( void )
{
	string homedir = getenv("HOME");
	if ( homedir == "" ) {
		homedir = string(getpwuid(getuid())->pw_dir);
	}
#ifdef DEBUG
	cout<<"Detected home:"<<homedir<<endl;
#endif
	return homedir;
}


void pp_daemon::read_set_conf( int argc, char** argv )
{
	
	
	//
	string dir = ".pp_app";
#ifdef __linux__
	dir = get_home_dir() + "/" + dir;
	if( test_if_dir_exists( dir ) == false ) {
		if( mkdir( dir.c_str(), 0777 ) < 0 ) {
			perror("Configuration Dir:");
			cerr<<"Failed to create:"<<dir.c_str()<<endl;
		}
	}
#endif
	string file_name = dir + "pp_app.conf";
	if( test_if_file_exists( file_name ) ) {
		cout<<"Loading conf"<<endl;
		load_conf_attr( file_name );		
	}else{
		cout<<"processing cli"<<endl;
		parse_cli_load_conf( file_name, argc, argv );
	}
	for( map<string,string>::iterator i = valid_attrs.begin(); 
	     i != valid_attrs.end(); ++i ) {
		cout<<"Val:"<<i->first<<" "<<i->second<<endl;
		if( i->second == "" ) {
			cerr<<"Configuration parameters are missing."<<endl;
			show_help();
			exit(EXIT_FAILURE);			
		}
		
	}
}

void pp_daemon::cleanup( ) 
{
	temp_values.erase( temp_values.begin(), temp_values.end() );
}

int 
main( int argc, char** argv )
{
	pp_daemon verify( argc, argv );
	while( true ) {
		cout<<"Getting data.."<<endl;
		verify.conn_and_get( );
		cout<<"Pushing data to DB.."<<endl;
		verify.push_data_to_db( );
		verify.cleanup();
		sleep(1); 
	}
	return 0;	
}
