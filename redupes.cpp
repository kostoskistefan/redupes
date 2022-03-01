#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>
#include "progressbar.hpp"

int find_symlink_files(const std::string &search_path, int max_file_size = 1000, int threads = 1)
{
    std::cout << "Searching for symlink files..." << std::endl;

    std::string command = 
        "find " + search_path + " -type f -size -" + std::to_string(max_file_size) + "c -print0 | " +
        "xargs -0 -P " + std::to_string(threads) + " grep -aE '^IntxLNK' | " +
        "tr -d '\\000' | tr -d '\\001' > " + "/tmp/redupes.txt";

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
    std::ofstream destination(destination_file, std::ios::binary);

    destination << source.rdbuf();
}

void safe_copy(const std::string &source, const std::string &destination)
{
    if (path_exists(source))
    {
        copy_file(source, destination + "_copy");

        std::remove(destination.c_str());

        std::rename((destination + "_copy").c_str(), destination.c_str());
    }
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
            Defaults to 1000 bytes.

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

    if (arguments.count("max_file_size") && !is_number(arguments.at("max_file_size")))
        exit_with_status(2, "Invalid size specified for MAX_FILE_SIZE.");

    if (arguments.count("threads") && !is_number(arguments.at("threads")))
        exit_with_status(2, "Invalid amount specified for THREADS.");

    return arguments;
}

int get_file_line_count()
{
    std::string command = "wc -l /tmp/redupes.txt > /tmp/redupes_count.txt";

    system(command.c_str());

    std::fstream count_file("/tmp/redupes_count.txt");

    std::string line;
    std::getline(count_file, line);

    int count = std::stoi(line);

    count_file.close();
    std::remove("/tmp/redupes_count.txt");

    return count;
}

int main(int argc, char *argv[])
{
    std::map<std::string, std::string> arguments = parse_arguments(argc, argv);

    std::fstream redupes;

    int find_symlink_status = find_symlink_files(arguments.at("search_path"));

    if (find_symlink_status == 0)
    {
        redupes.open("/tmp/redupes.txt");

        if (!redupes.is_open())
            exit_with_status(2, "Could not generate temporary redupes file. Exiting...");

        std::string line;

        float current_line_count = 0;
        size_t total_line_count = get_file_line_count();
        ProgressBar progress{std::clog, 70u, "Reduplicating"};

        if (arguments.count("replace_path"))
        {
            while(std::getline(redupes, line))
            {
                std::pair<std::string, std::string> paths = split_paths(line);

                paths.second = replace_path(paths.second, arguments.at("original_path"), arguments.at("replace_path"));

                safe_copy(paths.second, paths.first);

                current_line_count++;
                progress.write(current_line_count / total_line_count);
            }
        }

        else
        {
            while(std::getline(redupes, line))
            {
                std::pair<std::string, std::string> paths = split_paths(line);

                safe_copy(paths.second, paths.first);

                current_line_count++;
                progress.write(current_line_count / total_line_count);
            }
        }

        redupes.close();
    }

    return 0;
}
