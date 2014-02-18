// Copyright (C) 2014  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_STRUCTURAL_TRACK_ASSOCIATION_TRAnER_H__
#define DLIB_STRUCTURAL_TRACK_ASSOCIATION_TRAnER_H__

#include "structural_track_association_trainer_abstract.h"
#include "../algs.h"
#include "svm.h"
#include <utility>
#include "track_association_function.h"
#include "structural_assignment_trainer.h"
#include <map>

namespace dlib
{

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <
            typename detection_type,
            typename detection_id_type
            >
        std::vector<detection_type> get_unlabeled_dets (
            const std::vector<std::pair<detection_type,detection_id_type> >& dets
        )
        {
            std::vector<detection_type> temp;
            temp.reserve(dets.size());
            for (unsigned long i = 0; i < dets.size(); ++i)
                temp.push_back(dets[i].first);
            return temp;
        }

    }

// ----------------------------------------------------------------------------------------

    template <
        typename detection_type_,
        typename detection_id_type_ = unsigned long 
        >
    class structural_track_association_trainer
    {
    public:
        typedef detection_type_ detection_type;
        typedef typename detection_type::track_type track_type;
        typedef detection_id_type_ detection_id_type;
        typedef std::pair<detection_type, detection_id_type> labeled_detection;
        typedef std::vector<labeled_detection> detections_at_single_time_step;
        // This type logically represents an entire track history
        typedef std::vector<detections_at_single_time_step> sample_type;

        typedef track_association_function<detection_type> trained_function_type;

        structural_track_association_trainer (
        )  
        {
            set_defaults();
        }

        void set_num_threads (
            unsigned long num
        )
        {
            num_threads = num;
        }

        unsigned long get_num_threads (
        ) const
        {
            return num_threads;
        }

        void set_epsilon (
            double eps_
        )
        {
            // make sure requires clause is not broken
            DLIB_ASSERT(eps_ > 0,
                "\t void structural_track_association_trainer::set_epsilon()"
                << "\n\t eps_ must be greater than 0"
                << "\n\t eps_: " << eps_ 
                << "\n\t this: " << this
                );

            eps = eps_;
        }

        double get_epsilon (
        ) const { return eps; }

        void set_max_cache_size (
            unsigned long max_size
        )
        {
            max_cache_size = max_size;
        }

        unsigned long get_max_cache_size (
        ) const
        {
            return max_cache_size; 
        }

        void be_verbose (
        )
        {
            verbose = true;
        }

        void be_quiet (
        )
        {
            verbose = false;
        }

        void set_oca (
            const oca& item
        )
        {
            solver = item;
        }

        const oca get_oca (
        ) const
        {
            return solver;
        }

        void set_c (
            double C_ 
        )
        {
            // make sure requires clause is not broken
            DLIB_ASSERT(C_ > 0,
                "\t void structural_track_association_trainer::set_c()"
                << "\n\t C_ must be greater than 0"
                << "\n\t C_:    " << C_ 
                << "\n\t this: " << this
                );

            C = C_;
        }

        double get_c (
        ) const
        {
            return C;
        }

        bool learns_nonnegative_weights (
        ) const { return learn_nonnegative_weights; }
       
        void set_learns_nonnegative_weights (
            bool value
        )
        {
            learn_nonnegative_weights = value;
        }

        const track_association_function<detection_type> train (  
            const std::vector<sample_type>& samples
        ) const
        {
            // make sure requires clause is not broken
            DLIB_ASSERT(is_track_association_problem(samples),
                        "\t track_association_function structural_track_association_trainer::train()"
                        << "\n\t invalid inputs were given to this function"
                        << "\n\t is_track_association_problem(samples): " << is_track_association_problem(samples)
            );


            const unsigned long num_dims = find_num_dims(samples);

            feature_extractor_track_association<detection_type> fe(num_dims, learn_nonnegative_weights?num_dims:0);
            structural_assignment_trainer<feature_extractor_track_association<detection_type> > trainer(fe);


            if (verbose)
                trainer.be_verbose();

            trainer.set_c(C);
            trainer.set_epsilon(eps);
            trainer.set_max_cache_size(max_cache_size);
            trainer.set_num_threads(num_threads);
            trainer.set_oca(solver);

            std::vector<std::pair<std::vector<detection_type>, std::vector<track_type> > > assignment_samples;
            std::vector<std::vector<long> > labels;
            for (unsigned long i = 0; i < samples.size(); ++i)
                convert_dets_to_association_sets(samples[i], assignment_samples, labels);


            return track_association_function<detection_type>(trainer.train(assignment_samples, labels));
        }

        const track_association_function<detection_type> train (  
            const sample_type& sample
        ) const
        {
            std::vector<sample_type> samples;
            samples.push_back(sample);
            return train(samples);
        }

    private:

        static unsigned long find_num_dims (
            const std::vector<sample_type>& samples
        )
        {
            // find a detection_type object so we can call get_similarity_features() and
            // find out how big the feature vectors are.

            // for all detection histories 
            for (unsigned long i = 0; i < samples.size(); ++i)
            {
                // for all time instances in the detection history
                for (unsigned j = 0; j < samples[i].size(); ++j)
                {
                    if (samples[i][j].size() > 0)
                    {
                        track_type new_track;
                        typename track_type::feature_vector_type feats;
                        new_track.get_similarity_features(samples[i][j][0].first, feats);
                        return feats.size();
                    }
                }
            }

            DLIB_CASSERT(false, 
                "No detection objects were given in the call to dlib::structural_track_association_trainer::train()");
        }

        static void convert_dets_to_association_sets (
            const std::vector<detections_at_single_time_step>& det_history,
            std::vector<std::pair<std::vector<detection_type>, std::vector<track_type> > >& data,
            std::vector<std::vector<long> >& labels
        ) 
        {
            if (det_history.size() < 1)
                return;

            std::vector<track_type> tracks;
            // track_labels maps from detection labels to the index in tracks.  So track
            // with detection label X is at tracks[track_labels[X]].
            std::map<detection_id_type,unsigned long> track_labels;
            add_dets_to_tracks(tracks, track_labels, det_history[0]);

            using namespace impl;
            for (unsigned long i = 1; i < det_history.size(); ++i)
            {
                data.push_back(std::make_pair(get_unlabeled_dets(det_history[i]), tracks));
                labels.push_back(get_association_labels(det_history[i], track_labels));
                add_dets_to_tracks(tracks, track_labels, det_history[i]);
            }
        }

        static std::vector<long> get_association_labels(
            const std::vector<labeled_detection>& dets,
            const std::map<detection_id_type,unsigned long>& track_labels
        )
        {
            std::vector<long> assoc(dets.size(),-1);
            // find out which detections associate to what tracks
            for (unsigned long i = 0; i < dets.size(); ++i)
            {
                typename std::map<detection_id_type,unsigned long>::const_iterator j;
                j = track_labels.find(dets[i].second);
                // If this detection matches one of the tracks then record which track it
                // matched with.
                if (j != track_labels.end())
                    assoc[i] = j->second;
            }
            return assoc;
        }

        static void add_dets_to_tracks (
            std::vector<track_type>& tracks,
            std::map<detection_id_type,unsigned long>& track_labels,
            const std::vector<labeled_detection>& dets
        )
        {
            std::vector<bool> updated_track(tracks.size(), false);

            // first assign the dets to the tracks
            for (unsigned long i = 0; i < dets.size(); ++i)
            {
                const detection_id_type& label = dets[i].second;
                if (track_labels.count(label))
                {
                    const unsigned long track_idx = track_labels[label];
                    tracks[track_idx].update_track(dets[i].first);
                    updated_track[track_idx] = true;
                }
                else
                {
                    // this detection creates a new track
                    track_type new_track;
                    new_track.update_track(dets[i].first);
                    tracks.push_back(new_track);
                    track_labels[label] = tracks.size()-1;
                }

            }

            // Now propagate all the tracks that didn't get any detections.
            for (unsigned long i = 0; i < updated_track.size(); ++i)
            {
                if (!updated_track[i])
                    tracks[i].propagate_track();
            }
        }

        double C;
        oca solver;
        double eps;
        bool verbose;
        unsigned long num_threads;
        unsigned long max_cache_size;
        bool learn_nonnegative_weights;

        void set_defaults ()
        {
            C = 100;
            verbose = false;
            eps = 0.1;
            num_threads = 2;
            max_cache_size = 5;
            learn_nonnegative_weights = false;
        }
    };

}

#endif // DLIB_STRUCTURAL_TRACK_ASSOCIATION_TRAnER_H__
