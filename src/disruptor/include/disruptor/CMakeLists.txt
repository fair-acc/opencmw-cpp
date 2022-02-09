
set(Disruptor_sources
        ThreadHelper_Linux.cpp
        ThreadHelper_macOS.cpp
        ThreadHelper_Windows.cpp
        )

set(Disruptor_headers
        AggregateEventHandler.hpp
        BasicExecutor.hpp
        BatchEventProcessor.hpp
        BlockingQueue.hpp
        ConsumerRepository.hpp
        Disruptor.hpp
        EventHandlerGroup.hpp
        EventPoller.hpp
        EventProcessorInfo.hpp
        ExceptionBase.hpp
        ExceptionHandlerSetting.hpp
        ExceptionHandlerWrapper.hpp
        FatalExceptionHandler.hpp
        FixedSequenceGroup.hpp
        IConsumerInfo.hpp
        ICursored.hpp
        IDataProvider.hpp
        IEventHandler.hpp
        IEventProcessor.hpp
        IEventProcessorFactory.hpp
        IEventProcessorSequenceAware.hpp
        IEventReleaseAware.hpp
        IEventReleaser.hpp
        IEventSequencer.hpp
        IEventTranslator.hpp
        IEventTranslatorVararg.hpp
        IExceptionHandler.hpp
        IExecutor.hpp
        IgnoreExceptionHandler.hpp
        IHighestPublishedSequenceProvider.hpp
        ILifecycleAware.hpp
        ISequence.hpp
        ISequenceBarrier.hpp
        ISequenced.hpp
        ISequencer.hpp
        ISequenceReportingEventHandler.hpp
        ITaskScheduler.hpp
        ITimeoutHandler.hpp
        WaitStrategy.hpp
        IWorkHandler.hpp
        MultiProducerSequencer.hpp
        NoOpEventProcessor.hpp
        Pragmas.hpp
        ProcessingSequenceBarrier.hpp
        ProducerType.hpp
        RingBuffer.hpp
        RoundRobinThreadAffinedTaskScheduler.hpp
        Sequence.hpp
        SequenceGroups.hpp
        Sequencer.hpp
        SingleProducerSequencer.hpp
        SpinWait.hpp
        ThreadPerTaskScheduler.hpp
        ThreadHelper.hpp
        TypeInfo.hpp
        Util.hpp
        WorkerPool.hpp
        WorkerPoolInfo.hpp
        WorkProcessor.hpp
        )

add_library(DisruptorStatic STATIC ${Disruptor_sources})
set_target_properties(DisruptorStatic PROPERTIES OUTPUT_NAME Disruptor)
target_include_directories(DisruptorStatic INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")

