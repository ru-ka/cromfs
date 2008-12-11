#include <vector>
#include <algorithm>

/* Asymmetric Travelling Salesman Problem */

/* Note: This implementation aims to find the longest path
 * (i.e. largest distance), not the shortest path
 */

template<typename DistanceType>
class TravellingSalesmanProblem
{
private:
    const std::vector<DistanceType>& distances;
    const size_t num_nodes;

private:
    inline const DistanceType& GetDistance(size_t nodea, size_t nodeb) const
    {
        return distances[nodea * num_nodes + nodeb];
    }
    
public:
    /* Initialize the function with an array consisting of the distance*
     * of the nodes.  d.size() should equal to n*n.
     * Distance from A->B should be found at distances[A * n + B].
     * A->B may be different from B->A.
     */
    TravellingSalesmanProblem(const std::vector<DistanceType>& d, size_t n)
        : distances(d), num_nodes(n) { }

public:
    /* Interface function. Calculates and returns a solution. */
    /* Input: none */
    /* Output: list of node numbers */
    void Solve(std::vector<size_t>& solution)
    {
        if(!num_nodes) { solution.clear(); return; }
        solution.reserve(num_nodes);
        
        if(num_nodes <= 6)
        {
            // 6! is less than 6^4, but 7 is the other way around
            SolveBruteForce(solution);
        }
        else if(num_nodes <= 100) // 30^4 = 810000
            SolveExhaustiveRotations(solution);
        else if(num_nodes <= 200)
            SolveRandomPathChange(solution);
        else
            SolveNearestNeighbour(solution);

#ifdef TSP_DEBUG_POWER
        fprintf(stderr, "Solution power(%u nodes): %ld\n",
            (unsigned)num_nodes,
            (long)(EvaluateSolution(solution)));
#endif
    }
    
    /* Brute force algorithm. Guaranteed to find the best solution,
     * but is slow for large number of nodes.
     * Complexity: O(n!)
     */
    void SolveBruteForce(std::vector<size_t>& solution)
    {
        std::vector<size_t> option(num_nodes);
        for(size_t a=0; a<num_nodes; ++a) option[a] = a;
        DistanceType best_dist = 0;
        bool first = true;
        for(;;)
        {
            DistanceType dist = EvaluateSolution(option);
            if(first || best_dist < dist)
            {
                best_dist = dist;
                solution = option;
                first = false;
            }
            if(!std::next_permutation(option.begin(), option.end())) break;
        }
    }
    
    /* Greedy algorithm. Finds a solution quick, but not necessarily
     * the best solution.
     */
    void SolveNearestNeighbour(std::vector<size_t>& solution)
    {
        std::vector<size_t> favourites = BuildFavouriteLists();
        const unsigned bitset_bitness = 32;
        std::vector<uint_least32_t> done
            ( (num_nodes + bitset_bitness-1) / bitset_bitness, 0 );
        
        /* Start from the node that has the largest favourite. */
        size_t prev = 0; { size_t prev_score = 0;
        for(size_t a=0; a<num_nodes; ++a)
        {
            const size_t favourite = favourites[a*num_nodes+0];
            const DistanceType favscore = GetDistance(a, favourite);
            if(favscore > prev_score) { prev_score = favscore; prev = a; }
        } }
        solution.push_back(prev);
        while(solution.size() < num_nodes)
        {
            const unsigned prev_bit_index = prev / bitset_bitness;
            const unsigned prev_bit_value = 1U << (prev % bitset_bitness);
            done[prev_bit_index] |= prev_bit_value;
            
            size_t bestnode = 0;
            for(size_t a=0; a<num_nodes; ++a)
            {
                bestnode = favourites[prev*num_nodes + a];
                const unsigned done_bit_index = bestnode / bitset_bitness;
                const unsigned done_bit_value = 1U << (bestnode % bitset_bitness);
                
                if(!(done[done_bit_index] & done_bit_value)) break;
            }
            solution.push_back(bestnode);
            prev = bestnode;
        }
    }

    /* This resembles a genetic algorithm.
     */
    void SolveRandomPathChange(std::vector<size_t>& solution)
    {
        solution.resize(num_nodes);
        for(size_t a=0; a<num_nodes; ++a) solution[a] = a;
        DistanceType best = EvaluateSolution(solution);
        const size_t max_tries = std::min(num_nodes*num_nodes*10lu, 10000lu);
        const size_t max_span  = std::min(std::max(1lu, num_nodes/3lu), 50lu);
                                 // 1 <= n <= (num_nodes)/3
                                 //        <= 50
        for(;;)
        {
            for(size_t a=0; a<max_tries; ++a)
            {
                std::vector<size_t> attempt = solution;
                
                size_t pos3 = 1 + rand() % num_nodes;
                size_t pos1 = pos3 - 1 - std::min(max_span, rand() % pos3);
                size_t pos2 = pos1 + rand() % (pos3-pos1);
                std::rotate(attempt.begin() + pos1,
                            attempt.begin() + pos2,
                            attempt.begin() + pos3);
                DistanceType score = EvaluateSolution(attempt);
                if(score > best) { solution = attempt; best = score; goto ok; }
            }
            break;
         ok: continue;
        }
    }

    /* This changes the order of points until no change benefits.
     */
    void SolveExhaustiveRotations(std::vector<size_t>& solution)
    {
        solution.resize(num_nodes);
        for(size_t a=0; a<num_nodes; ++a) solution[a] = a;
        DistanceType best = EvaluateSolution(solution);
        /* Complexity: ?*N*(N*(N/2)*(N/4)) i.e. O(n^4) */
        std::vector<size_t> attempt;
        for(;;)
        {
            for(size_t a=0; a<num_nodes; ++a)
            for(size_t b=a+1; b<num_nodes; ++b)
            for(size_t c=a+1; c<b; ++c)
            {
                attempt = solution;
                std::rotate(attempt.begin() + a,
                            attempt.begin() + c,
                            attempt.begin() + b);
                DistanceType score = EvaluateSolution(attempt);
                if(score > best) { solution = attempt; best = score; goto ok; }
            }
            break;
         ok: continue;
        }
    }
 

private:
    struct FavouriteFinder
    {
        FavouriteFinder(const std::vector<DistanceType>& d, size_t n)
            : distances(d), num_nodes(n), whose() { }

        bool operator() (size_t a, size_t b) const
        {
            return distances[whose * num_nodes + a]
                 > distances[whose * num_nodes + b];
        }
    private:
        const std::vector<DistanceType>& distances;
        const size_t num_nodes;
    public:
        size_t whose;
    };
    
    /* For each node, calculates the relative favoriteness
     * of the other nodes (sorts other nodes in order of distance)
     */
    const std::vector<size_t> BuildFavouriteLists() const
    {
        std::vector<size_t> result ( num_nodes * num_nodes );
        FavouriteFinder find(distances, num_nodes);
        for(size_t a=0; a<num_nodes; ++a)
        {
            find.whose = a;
            
            size_t pos = a*num_nodes;
            for(size_t b=0; b<num_nodes; ++b)
                result[pos + b] = b;
            
            std::sort(result.begin() + pos,
                      result.begin() + pos + num_nodes,
                      find);
        }
        return result;
    }
    
    /* Returns the score of the given solution */
    const DistanceType EvaluateSolution(const std::vector<size_t>& solution) const
    {
        DistanceType dist=0;
        for(size_t b=solution.size(),a=1; a<b; ++a)
            dist += GetDistance(solution[a-1], solution[a]);
        return dist;
    }
};
