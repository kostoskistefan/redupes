#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>

int find_symlink_files(const std::string &search_path, int max_file_size = 1500, int threads = 1, std::string temp_directory = "/tmp/")
{
    std::string command = 
        "find " + search_path + " -type f -size -" + std::to_string(max_file_size) + "c -print0 | " +
        "xargs -0 -P " + std::to_string(threads) + " grep -aE '^IntxLNK' | " +
        "tr -d '\\000' | tr -d '\\001' > " + temp_directory + "redupes.txt";

    return system(command.c_str());
}

std::pair<std::string, std::string> split_paths(const std::string &line)
{
    std::pair<std::string, std::string> paths;

    std::size_t split_index = line.find(":IntxLNK");

    std::string local_file = line.substr(0, split_index);
    std::string symlink_location = line.substr(split_index + 8);

    paths.first = local_file;
    paths.second = symlink_location;

    return paths;
}

std::string replace_path(std::string path, const std::string &search, const std::string &replace)
{
    std::size_t found_index = path.find(search);

    if (found_index == path.npos)
        return path;

    return path.replace(found_index, search.length(), replace); 
}

bool path_exists(const std::string path)
{
    struct stat buffer;
    return stat(path.c_str(), &buffer) == 0;
}

void copy_file(const std::string &source_file, const std::string &destination_file)
{
    std::ifstream source(source_file, std::ios::binary);

    if (path_exists(source_file))
    {
        std::ofstream destination(destination_file, std::ios::binary);
        destination << source.rdbuf();
    }
}

void safe_copy(const std::string &source, const std::string &destination, std::string temp_directory = "/tmp/")
{
    copy_file(source, destination + "_copy");

    std::string base_filename = destination.substr(destination.find_last_of("/\\") + 1);
    copy_file(destination, temp_directory + base_filename);

    // std::remove(destination.c_str());
    std::rename((destination + "_copy").c_str(), destination.c_str());
}

void print_help_message()
{
    std::string help_message = R""""(
    Redupes 1.0.0
    Kostoski Stefan <kostoski.stefan90@gmail.com>

    Redupes - undoes deduplication made with rdfind and restores original files.


    USAGE: 
        redupes [OPTIONS] -s SEARCH_PATH 

    ARGS:
        <SEARCH_PATH>
            Path to the directory from which you wish to start the recursive search
            for deduplicated symlinks.

    OPTIONS:
        -h 
            Prints this help message

        -t <THREADS>
            Amount of threads used to search for symlink files. 
            Defaults to 1.

        -m <MAX_FILE_SIZE>
            Search for files with size under MAX_FILE_SIZE.
            The smaller the value you specify here the faster the search speed, 
            but setting it too low might miss some symlinks that link to a long path. 
            Defaults to 1500 bytes.

        -d <TEMP_DIRECTORY>
            Specifies which directory to use for storing temporary files.
            Defaults to /tmp.

        -o <ORIGINAL_PATH> 
            This option is used in combination with the -r flag and is used to 
            replace the symlink path. Read more below.

        -r <REPLACE_PATH>
            This option is used in combination with the -o flag and is used to 
            replace the symlink path. 

            For example, if you deduplicated files on a backup hard drive, 
            restoring the backup doesn't fix the symlinks and they still point to
            the backup hard drive. 

            In that case, use the -o and -r flags to replace the symlink path from 
            ORIGINAL_PATH to REPLACE_PATH.

            Ex. If we have a symlink picture /home/user/Pictures/image.jpg
            linking to /run/media/user/Backup_Drive/Linux/image.jpg,
            we could specify the following:
                SEARCH_PATH = /home/user/Pictures/
                ORIGINAL_PATH = /run/media/user/Backup_Drive/Linux
                REPLACE_PATH = /run/media/user/Windows_Partition/

            This will restore the /home/user/Pictures/image.jpg file from
            /run/media/user/Windows_Partition/image.jpg
    )"""";

    std::cout << help_message << std::endl;
    exit(0);
}

bool is_number(const std::string& str)
{
    return str.find_first_not_of("0123456789") == std::string::npos;
}

void exit_with_status(int status, const std::string &reason)
{
    std::cout << reason << std::endl;
    exit(status);
}

std::map<std::string, std::string> parse_arguments(int argc, char *argv[])
{
    for (size_t i = 1; i < argc; i++)
        if (((std::string) argv[i]).compare("-h") == 0)
            print_help_message();

    std::map<std::string, std::string> options = {
        {"-s", "search_path"},
        {"-t", "threads"},
        {"-m", "max_file_size"},
        {"-o", "original_path"},
        {"-r", "replace_path"},
        {"-d", "temp_directory"},
    };

    std::map<std::string, std::string> arguments;

    for (size_t i = 1; i < argc - 1; i += 2)
        arguments.insert({options.at(argv[i]), argv[i + 1]});

    if (!arguments.count("search_path"))
        exit_with_status(2, "Search path not specified.");

    else if (!path_exists(arguments.at("search_path")))
        exit_with_status(2, "Specified search path does not exist or is not accessible.");

    if (arguments.count("original_path") != arguments.count("replace_path"))
        exit_with_status(2, "You need to specify ORIGINAL_PATH and REPLACE_PATH to use the replace function.");

    if (arguments.count("replace_path") && !path_exists(arguments.at("replace_path")))
        exit_with_status(2, "Specified REPLACE_PATH does not exist or is not accessible.");

    if (arguments.count("temp_directory") && !path_exists(arguments.at("temp_directory")))
        exit_with_status(2, "Specified TEMP_DIRECTORY does not exist or is not accessible.");

    if (arguments.count("max_file_size") && !is_number(arguments.at("max_file_size")))
        exit_with_status(2, "Invalid size specified for MAX_FILE_SIZE.");

    if (arguments.count("threads") && !is_number(arguments.at("threads")))
        exit_with_status(2, "Invalid amount specified for THREADS.");

    return arguments;
}

int main(int argc, char *argv[])
{
    std::map<std::string, std::string> arguments = parse_arguments(argc, argv);

    std::fstream redupes;

    int find_symlink_status = find_symlink_files(arguments.at("search_path"));

    if (find_symlink_status == 0)
    {
        if (arguments.count("temp_directory"))
            redupes.open(arguments.at("temp_directory") + "/redupes.txt");
    
        else redupes.open("/tmp/redupes.txt");
    
        if (!redupes.is_open())
            exit_with_status(2, "Could not generate temporary redupes file. Exiting...");
    
        std::string line;
    
        if (arguments.count("replace_path"))
        {
            while(std::getline(redupes, line))
            {
                std::pair<std::string, std::string> paths = split_paths(line);
    
                paths.second = replace_path(paths.second, arguments.at("original_path"), arguments.at("replace_path"));
    
                safe_copy(paths.second, paths.first);
            }
        }
    
        else
        {
            while(std::getline(redupes, line))
            {
                std::pair<std::string, std::string> paths = split_paths(line);

                safe_copy(paths.second, paths.first);
            }
        }
    
        redupes.close();
    }

    return 0;
}
