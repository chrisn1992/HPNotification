Improved HP Notification plugin version 1.1
- Improve Config reading as JSON-to-object
- Utilise multi-threading and timing:
   + Separate main thread from monster threads to run monster health/size check independently.
   + Change chrono unit to ms.
   + Main thread for creating each monster thread should be looping each 1 second(1000ms).
   + Each Monster thread gets loop delay from config, and minimum at 2 seconds. (To-be improved).
   + Add pending flag to immediately wait and loop to prevent delay/duplicate messages.
   + Improve thread lock to wait for correct memory access.
- Improve Display crown: Crown size should be check immediately after checking valid HP instead of looping indefinitely.
- Fix and Improve display capture: Capture message now should not override Health message.
- Add more stylish message display and more information
- Add message display type:
   - Interval(check every {TypeValue}ms): Display generic HP Message
   - Queue(Using RatioMessage): Display styled message or generic HP Message if styled message is not valid, interval check using  {TypeValue}ms (To-be improved).
   - HP:(check HP drain): loop for {TypeValue}HP drained every 1 second (To-be improved).
   - Percentage(Check HP%):loop for {TypeValue}% drained every 1 second (To-be improved).
