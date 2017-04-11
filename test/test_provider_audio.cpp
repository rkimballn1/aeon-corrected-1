/*
 Copyright 2016 Nervana Systems Inc.
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include "gtest/gtest.h"

#include <typeinfo>

#include "provider_factory.hpp"
#include "etl_audio.hpp"
#include "etl_label.hpp"
#include "etl_char_map.hpp"
#include "wav_data.hpp"
#include "json.hpp"
#include "util.hpp"
#include "log.hpp"

using namespace std;
using namespace nervana;

TEST(provider, audio_classify)
{
    nlohmann::json js_label = {{"type", "label"}, {"binary", true}};
    nlohmann::json js_audio = {{"type", "audio"},
                               {"max_duration", "2000 milliseconds"},
                               {"frame_length", "1024 samples"},
                               {"frame_stride", "256 samples"},
                               {"sample_freq_hz", 44100},
                               {"feature_type", "specgram"}};
    nlohmann::json js = {{"etl", {js_audio, js_label}}};

    auto media   = nervana::provider_factory::create(js);
    auto oshapes = media->get_output_shapes();

    auto buf_names = media->get_buffer_names();
    ASSERT_EQ(2, buf_names.size());
    ASSERT_NE(find(buf_names.begin(), buf_names.end(), "audio"), buf_names.end());
    ASSERT_NE(find(buf_names.begin(), buf_names.end(), "label"), buf_names.end());

    size_t batch_size = 128;

    wav_data     wav(sinewave_generator(400, 200), 3, 16000, false);
    vector<char> buf(wav_data::HEADER_SIZE + wav.nbytes());
    wav.write_to_buffer(&buf[0], buf.size());

    fixed_buffer_map    out_buf(oshapes, batch_size);
    encoded_record_list bp;

    for (int i = 0; i < batch_size; i++)
    {
        encoded_record record;
        record.add_element(buf);
        vector<char> packed_int(4);
        pack<int>(&packed_int[0], 42 + i);
        record.add_element(packed_int);
        bp.add_record(record);
    }

    EXPECT_EQ(bp.size(), batch_size);

    for (int i = 0; i < batch_size; i++)
    {
        media->provide(i, bp, out_buf);
    }

    for (int i = 0; i < batch_size; i++)
    {
        int target_value = unpack<int>(out_buf["label"]->get_item(i));
        EXPECT_EQ(42 + i, target_value);
    }
}

TEST(provider, transcript_length_check)
{
    uint32_t       max_length    = 15;
    nlohmann::json js_transcript = {{{"type", "char_map"},
                                     {"alphabet", "abcdefgß "},
                                     {"max_length", max_length},
                                     {"emit_length", true}}};

    nlohmann::json js = {{"etl", js_transcript}};

    auto media     = nervana::provider_factory::create(js);
    auto oshapes   = media->get_output_shapes();
    auto buf_names = media->get_buffer_names();

    // Ensure that we have two output buffers (an extra one for the transcript length since emit_length == true)
    ASSERT_EQ(2, buf_names.size());
    ASSERT_NE(find(buf_names.begin(), buf_names.end(), "char_map"), buf_names.end());
    ASSERT_NE(find(buf_names.begin(), buf_names.end(), "length"), buf_names.end());

    size_t batch_size = 4;

    fixed_buffer_map                   out_buf(oshapes, batch_size);
    encoded_record_list                bp;
    std::vector<string>                transcripts{"abcß", "ßßad", "abcabc", "ddefggf"};
    std::vector<uint32_t>              expected_lengths{4, 4, 6, 7};
    std::vector<std::vector<uint32_t>> expected_encodings{
        {0, 1, 2, 7}, {7, 7, 0, 3}, {0, 1, 2, 0, 1, 2}, {3, 3, 4, 5, 6, 6, 5}};
    for (auto&& s : transcripts)
    {
        encoded_record record;
        record.add_element(s.data(), s.length());
        bp.add_record(record);
    }

    EXPECT_EQ(bp.size(), batch_size);

    for (int i = 0; i < batch_size; i++)
    {
        media->provide(i, bp, out_buf);
    }

    // Check that the lengths are emitted as expected
    for (int i = 0; i < batch_size; i++)
    {
        uint32_t target_length = unpack<uint32_t>(out_buf["length"]->get_item(i));
        EXPECT_EQ(target_length, expected_lengths[i]);
    }

    for (int i = 0; i < batch_size; i++)
    {
        uint32_t* loaded_transcript = (uint32_t*)out_buf["char_map"]->get_item(i);

        for (uint32_t j = 0; j < max_length; ++j)
        {
            if (j < expected_encodings[i].size())
            {
                EXPECT_EQ(loaded_transcript[j], expected_encodings[i][j]);
            }
            else
            {
                EXPECT_EQ(loaded_transcript[j], 0);
            }
        }
    }
}

#warning this needs to be fixed
// TEST(provider, audio_transcript)
// {
//     nlohmann::json js = {{"type", "audio,transcription"},
//                          {"audio",
//                           {{"max_duration", "2000 milliseconds"},
//                            {"frame_length", "1024 samples"},
//                            {"frame_stride", "256 samples"},
//                            {"sample_freq_hz", 44100},
//                            {"feature_type", "specgram"}}},
//                          {"transcription",
//                           {{"alphabet", "ABCDEFGHIJKLMNOPQRSTUVWXYZ .,()"},
//                            {"pack_for_ctc", true},
//                            {"max_length", 50}}}};

//     // Create the config
//     auto media = dynamic_pointer_cast<audio_transcriber>(nervana::provider_factory::create(js));

//     // Get the character map
//     auto cmap = media->get_cmap();

//     size_t batch_size = 128;

//     // Generate a simple sine wav
//     float              sine_freq = 400;
//     int16_t            sine_ampl = 500;
//     sinewave_generator sg{sine_freq, sine_ampl};
//     int                wav_len_sec = 4, sample_freq = 44100;
//     bool               stereo = false;

//     wav_data     wav(sg, wav_len_sec, sample_freq, stereo);
//     vector<char> buf(wav_data::HEADER_SIZE + wav.nbytes());
//     wav.write_to_buffer(&buf[0], buf.size());

//     // Generate alternating fake transcripts
//     vector<string> tr{"The quick brown fox jumped over the lazy dog",
//                       "A much more interesting sentence."};
//     vector<char> tr0_char(tr[0].begin(), tr[0].end());
//     vector<char> tr1_char(tr[1].begin(), tr[1].end());

//     // Create the input buffer
//     encoded_record_list bp;
//     for (int i = 0; i < batch_size; i++)
//     {
//         encoded_record record;
//         record.add_element(buf);
//         record.add_element(((i % 2) == 0 ? tr0_char : tr1_char));
//         bp.add_record(record);
//     }
//     EXPECT_EQ(bp.size(), batch_size);
//     EXPECT_EQ(bp.size(), batch_size);

//     // Generate output buffers using shapes from the provider
//     auto             oshapes = media->get_output_shapes();
//     fixed_buffer_map out_buf(oshapes, batch_size);

//     // Call the provider
//     for (int i = 0; i < batch_size; i++)
//     {
//         media->provide(i, bp, out_buf);
//     }

//     // Check target sequences against their source string
//     for (int i = 0; i < batch_size; i++)
//     {
//         char* target_out  = out_buf["transcription"]->get_item(i);
//         auto  orig_string = tr[i % 2];
//         for (auto c : orig_string)
//         {
//             ASSERT_EQ(unpack<uint8_t>(target_out++), cmap[std::toupper(c)]);
//         }
//     }

//     // Check the transcript lengths match source string length
//     for (int i = 0; i < batch_size; i++)
//     {
//         ASSERT_EQ(unpack<uint32_t>(out_buf["trans_length"]->get_item(i)), tr[i % 2].length());
//     }

//     for (int i = 0; i < batch_size; i++)
//     {
//         ASSERT_EQ(unpack<uint32_t>(out_buf["valid_pct"]->get_item(i)), 100);
//     }

//     // Do the packing
//     media->post_process(out_buf);
//     string   combined_string = tr[0] + tr[1];
//     uint32_t packed_length   = combined_string.size() * batch_size / 2;

//     // Check that target sequence contains abutted vals corresponding to original strings
//     char* target_ptr = out_buf["transcription"]->data();
//     for (int i = 0; i < packed_length; i++)
//     {
//         char c = combined_string[i % combined_string.size()];
//         ASSERT_EQ(unpack<uint8_t>(target_ptr++), cmap[std::toupper(c)]);
//     }
//     for (int i = packed_length; i < out_buf["transcription"]->size(); i++)
//     {
//         ASSERT_EQ(0, *(target_ptr++));
//     }
// }
