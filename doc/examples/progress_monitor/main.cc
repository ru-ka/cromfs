#include <map>
#include <string>
#include <utility>
#include <vector>

#include <string.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    std::map<std::string, unsigned> prev_lines;
    std::map<unsigned, std::string> used_lines;
    std::vector<std::string> found;

    FILE* fp = fopen(".watch_keep.state", "rb");
    if(fp)
    {
        for(;;)
        {
            int lineno = getw(fp);
            if(lineno == EOF) break;
            int len = getw(fp);
            if(len == EOF) break;
            std::vector<char> buf(len);
            fread(&buf[0], 1, len, fp);
            std::string tmp(&buf[0], &buf[len]);
            prev_lines[tmp] = lineno;
        }
        fclose(fp);
    }
    
    {
      std::map<std::string, unsigned> new_prev_lines;
      fp = popen(argv[1], "r");
      for(;;)
      {
          char Buf[4096];
          if(!fgets(Buf, sizeof Buf, fp)) break;
          strtok(Buf, "\r");
          strtok(Buf, "\n");
          
          std::string line = Buf;
          
          std::map<std::string, unsigned>::const_iterator
              i = prev_lines.find(line);

          if(i != prev_lines.end())
          {
              used_lines.insert( std::make_pair(i->second, line) );
              new_prev_lines[i->first] = i->second;
              continue;
          }
          
          found.push_back(line);
      }
      prev_lines.swap(new_prev_lines);
    }
    
    for(size_t a=0; a<found.size(); ++a)
    {
        const std::string& line = found[a];
        
        unsigned lineno = 0;
        while(used_lines.find(lineno) != used_lines.end())
            ++lineno;
        
        used_lines.insert( std::make_pair(lineno, line) );
        prev_lines[line] = lineno;
    }
    pclose(fp);
    
    unsigned lineno = 0;
    for(std::map<unsigned, std::string>::const_iterator
        i = used_lines.begin();
        i != used_lines.end();
        ++i)
    {
        while(lineno < i->first) { putchar('\n'); ++lineno; }
        puts(i->second.c_str());
        ++lineno;
    }
    
    fp = fopen(".watch_keep.state", "wb");
    if(fp)
    {
        for(std::map<std::string, unsigned>::const_iterator
            i = prev_lines.begin();
            i != prev_lines.end();
            ++i)
        {
            putw(i->second, fp);
            putw(i->first.size(), fp);
            fwrite(i->first.data(), 1, i->first.size(), fp);
        }
        fclose(fp);
    }
}
