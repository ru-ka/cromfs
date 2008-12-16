/* An algorithm core for "shortest common superstring" */

/* It does so by translating it into an asymmetric Travelling Salesman problem. */

#include "boyermooreneedle.hh"
#include "asymmetrictsp.hh"
#include <algorithm>

template<typename UserDataType>
class SuperStringFinder
{
public:
    typedef BoyerMooreNeedle* DataType;

    struct DataInfo
    {
        DataType data;
        DataType substring_of;
        UserDataType userdata;

    public:
        DataInfo() : data(0), substring_of(0), userdata()
            { }

        bool contains(const DataInfo& b) const
        {
            return b.data->SearchIn(data->data(), data->size())
                 < data->size();
        }

        size_t lr_match_length(const DataInfo& right) const
        {
            const BoyerMooreNeedle& needle = *right.data;

            size_t appendpos =
                 needle.SearchInWithAppendOnly(data->data(), data->size());

            return data->size() - appendpos;
        }
    };

public:
    std::vector<DataInfo> input_data;
    size_t num_nonsubstrings;

    SuperStringFinder() : input_data(), num_nonsubstrings(0)
    {
    }

    void AddData(DataType p, UserDataType userdata)
    {
        DataInfo d;
        d.data = p;
        d.userdata = userdata;

        for(size_t a=0; a<input_data.size(); ++a)
        {
        recheck_a:
            if(input_data[a].contains(d))
            {
                d.substring_of = input_data[a].data;
                break;
            }
            if(!input_data[a].substring_of && d.contains(input_data[a]))
            {
                input_data[a].substring_of = d.data;

                std::rotate( input_data.begin() + a,
                             input_data.begin() + a + 1,
                             input_data.begin() + num_nonsubstrings
                           );
                --num_nonsubstrings;
                goto recheck_a;
            }
        }

        if(d.substring_of)
            input_data.push_back(d);
        else
            input_data.insert(input_data.begin() + num_nonsubstrings++,
                              d);
    }

    void Organize(std::vector<UserDataType>& result)
    {
        const size_t num_inputs = num_nonsubstrings;

        std::vector<size_t> lr_match_length ( num_inputs * num_inputs, 0 );

        for(size_t a=0; a<num_inputs; ++a)
        {
            const DataInfo& ad = input_data[a];
            //if(ad.substring_of) continue;

            for(size_t b=0; b<num_inputs; ++b)
            {
                const DataInfo& bd = input_data[b];
                //if(a == b || bd.substring_of) continue;

                const size_t lr = ad.lr_match_length(bd);

                lr_match_length[a * num_inputs + b] = lr;
                /*
                fprintf(stderr, "(%s)-(%s): %d\n",
                    ad.data->data(),
                    bd.data->data(),
                    lr);
                */
            }
        }
        /* The rest of this problem is actually an
         * Asymmetric Travelling Salesman Problem. */

        TravellingSalesmanProblem<size_t> solver (lr_match_length, num_inputs);
        std::vector<size_t> solution;
        solver.Solve(solution);

        result.clear();
        result.reserve(input_data.size());
        for(size_t b = solution.size(), a = 0; a < b; ++a)
            result.push_back(input_data[solution[a]].userdata);
        for(size_t b = input_data.size(), a = num_inputs; a < b; ++a)
            result.push_back(input_data[a].userdata);
    }
};
