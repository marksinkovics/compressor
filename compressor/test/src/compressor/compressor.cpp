#include <sstream>

#include <gtest/gtest.h>

#include <compressor/test/mock/task/decoder.h>
#include <compressor/test/mock/task/encoder.h>

#include <compressor/data/data.h>
#include <compressor/data/reader.h>
#include <compressor/test/mock/data/reader.h>
#include <compressor/test/mock/data/writer.h>
#include <compressor/test/mock/engine/engine.h>

class CompressorTests : public ::testing::Test
{
protected:
    void SetUp() override {}
};

TEST_F(CompressorTests, Encoding)
{
    std::string input_file{"input_file"};
    std::string output_file{"output_file"};
    compressor::DecodedData decoded_data;
    compressor::EncodedData encoded_data;

    MockIDataReader<compressor::DecodedData> reader;
    EXPECT_CALL(reader, open(testing::StrEq(input_file))).Times(1);
    EXPECT_CALL(reader, read()).WillOnce(testing::Return(decoded_data));
    EXPECT_CALL(reader, close()).Times(1);

    MockIEngine engine;
    EXPECT_CALL(engine, encode(testing::Eq(decoded_data))).WillOnce(testing::Return(encoded_data));

    MockIDataWriter<compressor::EncodedData> writer;
    EXPECT_CALL(writer, open(testing::StrEq(output_file))).Times(1);
    EXPECT_CALL(writer, write(testing::Eq(encoded_data))).Times(1);
    EXPECT_CALL(writer, close()).Times(1);

    testing::NiceMock<MockIEncoderTask> task;
    ON_CALL(task, input_file()).WillByDefault(testing::Return(input_file));
    ON_CALL(task, output_file()).WillByDefault(testing::Return(output_file));
    ON_CALL(task, engine()).WillByDefault(testing::ReturnRef(engine));
    ON_CALL(task, reader()).WillByDefault(testing::ReturnRef(reader));
    ON_CALL(task, writer()).WillByDefault(testing::ReturnRef(writer));

    compressor::Compressor compressor;
    compressor.encode(task);
}

TEST_F(CompressorTests, Decoding)
{
    std::string input_file{"input_file"};
    std::string output_file{"output_file"};
    compressor::DecodedData decoded_data;
    compressor::EncodedData encoded_data;

    MockIDataReader<compressor::EncodedData> reader;
    EXPECT_CALL(reader, open(testing::StrEq(input_file))).Times(1);
    EXPECT_CALL(reader, read()).WillOnce(testing::Return(encoded_data));
    EXPECT_CALL(reader, close()).Times(1);

    MockIEngine engine;
    EXPECT_CALL(engine, decode(testing::Eq(encoded_data))).WillOnce(testing::Return(decoded_data));

    MockIDataWriter<compressor::DecodedData> writer;
    EXPECT_CALL(writer, open(testing::StrEq(output_file))).Times(1);
    EXPECT_CALL(writer, write(testing::Eq(decoded_data))).Times(1);
    EXPECT_CALL(writer, close()).Times(1);

    testing::NiceMock<MockIDecoderTask> task;
    ON_CALL(task, input_file()).WillByDefault(testing::Return(input_file));
    ON_CALL(task, output_file()).WillByDefault(testing::Return(output_file));
    ON_CALL(task, engine()).WillByDefault(testing::ReturnRef(engine));
    ON_CALL(task, reader()).WillByDefault(testing::ReturnRef(reader));
    ON_CALL(task, writer()).WillByDefault(testing::ReturnRef(writer));

    compressor::Compressor compressor;
    compressor.decode(task);
}

// Disabled until Phase 2 checks the result of open().
TEST_F(CompressorTests, DISABLED_Encoding_stops_when_the_input_cannot_be_opened)
{
    testing::NiceMock<MockIDataReader<compressor::DecodedData>> reader;
    EXPECT_CALL(reader, open(testing::_)).WillOnce(testing::Return(false));
    EXPECT_CALL(reader, read()).Times(0);

    MockIEngine engine;
    EXPECT_CALL(engine, encode(testing::_)).Times(0);

    MockIDataWriter<compressor::EncodedData> writer;
    EXPECT_CALL(writer, open(testing::_)).Times(0);
    EXPECT_CALL(writer, write(testing::_)).Times(0);

    testing::NiceMock<MockIEncoderTask> task;
    ON_CALL(task, input_file()).WillByDefault(testing::Return("input_file"));
    ON_CALL(task, output_file()).WillByDefault(testing::Return("output_file"));
    ON_CALL(task, engine()).WillByDefault(testing::ReturnRef(engine));
    ON_CALL(task, reader()).WillByDefault(testing::ReturnRef(reader));
    ON_CALL(task, writer()).WillByDefault(testing::ReturnRef(writer));

    compressor::Compressor compressor;
    compressor.encode(task);
}

TEST_F(CompressorTests, DISABLED_Decoding_stops_when_the_input_cannot_be_opened)
{
    testing::NiceMock<MockIDataReader<compressor::EncodedData>> reader;
    EXPECT_CALL(reader, open(testing::_)).WillOnce(testing::Return(false));
    EXPECT_CALL(reader, read()).Times(0);

    MockIEngine engine;
    EXPECT_CALL(engine, decode(testing::_)).Times(0);

    MockIDataWriter<compressor::DecodedData> writer;
    EXPECT_CALL(writer, open(testing::_)).Times(0);
    EXPECT_CALL(writer, write(testing::_)).Times(0);

    testing::NiceMock<MockIDecoderTask> task;
    ON_CALL(task, input_file()).WillByDefault(testing::Return("input_file"));
    ON_CALL(task, output_file()).WillByDefault(testing::Return("output_file"));
    ON_CALL(task, engine()).WillByDefault(testing::ReturnRef(engine));
    ON_CALL(task, reader()).WillByDefault(testing::ReturnRef(reader));
    ON_CALL(task, writer()).WillByDefault(testing::ReturnRef(writer));

    compressor::Compressor compressor;
    compressor.decode(task);
}
