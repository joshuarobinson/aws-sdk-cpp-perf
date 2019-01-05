/*
 * Multi-threaded bucket-reader.
 *
 * Uses std::thread to read all bucket objects in parallel.
 */

#include <chrono>
#include <fstream>
#include <list>
#include <mutex>
#include <optional>
#include <thread>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/Object.h>

// Constants
static const auto ThreadCount = 32U;
// Limit each GET to certain size to avoid overflowing memory.
static const auto ReadChunkSize = 4ULL * 1024ULL * 1024ULL * 1024ULL;  // 4GB


// Container class for a thread-safe queue of objects to read.
class ObjectList
{
  public:
    ObjectList() {}
   
    // Typedef for a GET operation: a key and byte range to read. 
    typedef std::pair<Aws::String, Aws::String> ReadKey;

    void push_back(const Aws::String & key, const Aws::String & range)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        keylist_.emplace_back(key, range);
    }

    // Remove an item from the queue. Returns null if no work currently available.
    std::optional<ReadKey> Pop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (keylist_.empty())
        {
            return {};
        }

        auto result = *keylist_.begin();
        keylist_.pop_front();
        return result;
    }

    void SetEnd()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        end_marker_ = true;
    }

    bool IsEnd()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return end_marker_;
    }

  private:
    std::list<ReadKey> keylist_;
    bool end_marker_ = false;
    std::mutex mutex_;
};


// Helper function to construct the range string.
Aws::String ConstructByteRange(const auto lower, const  auto upper)
{
    // Example output: "bytes=0-5"
    return "bytes=" + Aws::Utils::StringUtils::to_string(lower) + "-" +
                Aws::Utils::StringUtils::to_string(upper);
}

// Retrieve a given byte range of an object.
void RetrieveObjectRange(const Aws::S3::S3Client & s3_client,
                         const Aws::String & bucket_name,
                         const Aws::String & key,
                         const Aws::String & range)
{
    Aws::S3::Model::GetObjectRequest object_request;
    object_request.WithBucket(bucket_name).WithKey(key).WithRange(range);

    // Override the ResponseStreamFactory to customize what is done with the
    // data as it's read. The default is to store in a string, but below
    // instead redirects to /dev/null, effectively disgarding the data immediately.
    object_request.SetResponseStreamFactory([]() {
            return Aws::New<Aws::FStream>("alloc_tag", "/dev/null", std::ios_base::out); });

    auto get_object_outcome = s3_client.GetObject(object_request);

    if (get_object_outcome.IsSuccess())
    {
        std::cout << "Read " << key << " " << object_request.GetRange() <<
            std::endl;
    }
    else
    {
        std::cout << "GetObject error: " <<
            get_object_outcome.GetError().GetExceptionName() << " " <<
            get_object_outcome.GetError().GetMessage() << std::endl;
        return;
    }
}


// Thread loop that pulls work off of queue (ObjectList) until IsEnd() reached.
void Retriever(ObjectList & objects, const Aws::S3::S3Client & s3_client,
               const Aws::String & bucket_name)
{
    while (true)
    {
        while (const auto keyrange = objects.Pop())
        {
            const auto & key = keyrange->first;
            const auto & range = keyrange->second;
            RetrieveObjectRange(s3_client, bucket_name, key, range);
        }

        if (objects.IsEnd())  // Check if any further work is expected.
        {
            return;
        }

        // Wait for more work to appear.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


int main(int argc, char ** argv)
{
    Aws::SDKOptions options;
    Aws::InitAPI(options);
        
    if (argc < 3)
	{
        std::cout << std::endl <<
            "To run this example, supply the name of a bucket to list!" <<
            std::endl << "Ex: " << argv[0] << " <endpoint> <bucket-name>" << std::endl
            << std::endl;
        exit(1);
    }

    const Aws::String endpoint = argv[1];
    const Aws::String bucket_name = argv[2];

    // Define a work queue to coordinate the work done by all threads.
    ObjectList objects;

    std::cout << "Reading S3 bucket: " << bucket_name << " from " << endpoint
        << std::endl;

    // Configure the AWS client to connect to the endpoint specified on command line.
    Aws::Client::ClientConfiguration config;
    config.endpointOverride = endpoint;
    config.scheme = Aws::Http::Scheme::HTTP;
    config.maxConnections = ThreadCount;  // Allow each thread to open a TCP connection for max concurrency.

    // The fourth argument in the constructor disables path-style addressing,
    // which is not supported by many non-AWS object stores.
    Aws::S3::S3Client s3_client(config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

    // Start each thread in the read loop waiting for new work to appear.
    std::vector<std::thread> reader_threads(ThreadCount);
    for (auto & t : reader_threads)
    {
        t = std::thread(Retriever, std::ref(objects), s3_client, bucket_name);
    }

    // This main thread then continues on to LIST all objects and add them to the work queue.
    Aws::S3::Model::ListObjectsV2Request objects_request;
    objects_request.WithBucket(bucket_name);

    Aws::S3::Model::ListObjectsV2Outcome list_objects_outcome;

    do
    {
        list_objects_outcome = s3_client.ListObjectsV2(objects_request);

        if (list_objects_outcome.IsSuccess())
        {
            Aws::Vector<Aws::S3::Model::Object> object_list =
                list_objects_outcome.GetResult().GetContents();

            // For each object, add the key and chunked byte ranges to the ObjectList work queue.
            for (auto const & s3_object : object_list)
            {
                auto offset = 0ULL;

                const auto objsize = static_cast<unsigned long long>(s3_object.GetSize());

                while (offset < objsize)
                {
                    const auto upper_bound = std::min(objsize, offset + ReadChunkSize);
                    objects.push_back(s3_object.GetKey(), ConstructByteRange(offset, upper_bound));

                    offset += ReadChunkSize;  // Advance to the next chunk
                }
            }
        }
        else
        {
            std::cout << "ListObjects error: " <<
                list_objects_outcome.GetError().GetExceptionName() << " " <<
                list_objects_outcome.GetError().GetMessage() << std::endl;
        }

        // Continue listing remaining objects.
        objects_request.WithContinuationToken(list_objects_outcome.GetResult().GetNextContinuationToken());

    } while (list_objects_outcome.GetResult().GetIsTruncated());

    objects.SetEnd();  // Indicate that no additional work will be added.
   
    // Block for completion of all reader threads. 
    for (auto & t : reader_threads)
    {
        t.join();
    }

    Aws::ShutdownAPI(options);
}

