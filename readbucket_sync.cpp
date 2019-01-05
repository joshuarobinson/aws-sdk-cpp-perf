/*
 * Single-threaded bucket-reader.
 *
 * Synchronously LISTs bucket contents and issues GET for each object.
 */

#include <fstream>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/Object.h>

// Helper function to construct the range string.
Aws::String ConstructByteRange(const auto lower, const  auto upper)
{
    // Example output: "bytes=0-5"
    return "bytes=" + Aws::Utils::StringUtils::to_string(lower) + "-" +
                Aws::Utils::StringUtils::to_string(upper);
}

// Helper function to synchronously read a given S3 object.
void RetrieveObject(const Aws::S3::S3Client & s3_client,
                    const Aws::String & bucket_name,
                    const Aws::S3::Model::Object & s3_object)
{
    Aws::S3::Model::GetObjectRequest object_request;
    object_request.WithBucket(bucket_name).WithKey(s3_object.GetKey());
    
    // Redirect the reads to /dev/null.
    object_request.SetResponseStreamFactory([]() {
         return Aws::New<Aws::FStream>("alloc_tag", "/dev/null", std::ios_base::out); });
        

    auto get_object_outcome = s3_client.GetObject(object_request);

    if (get_object_outcome.IsSuccess())
    {
        std::cout << "Read " << s3_object.GetKey() << " " <<
            object_request.GetRange() << std::endl;
    }
    else
    {
        std::cout << "GetObject error: " <<
            get_object_outcome.GetError().GetExceptionName() << " " <<
            get_object_outcome.GetError().GetMessage() << std::endl;
        return;
    }
}

int main(int argc, char ** argv)
{
    Aws::SDKOptions options;
    Aws::InitAPI(options);
        
    if (argc < 3)
	{
        std::cout << std::endl <<
            "To run this example, supply the endpoint IP and name of a bucket to read. " <<
            std::endl << "Ex: " << argv[0] << " <endpoint> <bucket-name>" << std::endl
            << std::endl;
        exit(1);
    }

    const Aws::String endpoint = argv[1];
    const Aws::String bucket_name = argv[2];

    std::cout << "Reading S3 bucket: " << bucket_name << " from " << endpoint
        << std::endl;

    Aws::Client::ClientConfiguration config;
    config.endpointOverride = endpoint;
    config.scheme = Aws::Http::Scheme::HTTP;
    Aws::S3::S3Client s3_client(config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

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

            for (const auto & s3_object : object_list)
            {
                std::cout << "* " << s3_object.GetKey() << std::endl;

                RetrieveObject(s3_client, bucket_name, s3_object);
            }
        }
        else
        {
            std::cout << "ListObjects error: " <<
                list_objects_outcome.GetError().GetExceptionName() << " " <<
                list_objects_outcome.GetError().GetMessage() << std::endl;
        }

        objects_request.WithContinuationToken(list_objects_outcome.GetResult().GetNextContinuationToken());

    } while (list_objects_outcome.GetResult().GetIsTruncated());

    Aws::ShutdownAPI(options);
}

