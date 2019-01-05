# aws-sdk-cpp-perf

Build the SDK and example programs simply by building the included Docker image.

Connect the SDK to a FlashBlade requires setting the following configurations:
```
Aws::Client::ClientConfiguration::endpointOverride <== FlashBlade Data VIP
Aws::Client::ClientConfiguration::scheme = Aws::Http::Scheme::HTTP;  (OPTIONAL)
```

To provide S3 access keys, you will also need to create a 'credentials' file as
per [these
instructions](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/credentials.html).

Build Docker image with the following command:
```
docker build -t aws-s3-cpp .
```

Once built, an example invocation is:
```
docker run -it --name=aws-cpp-readbucket -v ${PWD}/credentials:/home/ir/.aws/credentials --rm aws-s3-cpp ./readbucket_mt 10.0.5.10 bucketname
```

The multi-threaded version uses one primary thread to LIST all objects in a
bucket and add them to a work queue. Then, multiple reader threads pull from
that queue and issue the GET operations. The GET results (contents) are
discarded so this benchmark only focuses on IO performance.

Use the following command set to look into the running container and count the
number of active TCP connections:
```
PID=$(docker inspect --format '{{.State.Pid}}' aws-cpp-readbucket); sudo nsenter -t $PID -n netstat -antp | grep ESTABLISHED | grep -c ":80"
```

It is not recommended to use the API based on std::future, GetObjectCallable,
as this API does not provide sufficient control or composability for high
performance async code.
